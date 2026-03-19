// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_execution_context.h"

#include "devices/log.h"
#include "devices/physical_timer.h"

#include <minimalstdio.h>

extern "C" void SwitchCPUState(task::TaskImpl::TaskContextCPUState *prev, task::TaskImpl::TaskContextCPUState *next);

namespace task
{
    void TaskExecutionContext::ServiceMessages()
    {
        //  Process any queued messages

        InterContextMessage message;

        while (inter_context_message_queue_.PopMessage(message))
        {
            switch (message.Type())
            {
            case InterContextMessage::MessageType::ADD_TASK:
                //  Add the task to the task list
                task_list_.AddTask(message.Task());
                break;

            case InterContextMessage::MessageType::SURRENDER_TASK:
                //  TODO Implement
                break;

            default:
                LogError("TaskExecutionContext::ServiceMessages - Unknown message type: %d\n", (uint32_t)message.Type());
                break;
            }
        }
    }

    TaskImpl &TaskExecutionContext::FindNextTask(void)
    {
        long max_counter = -1;
        TaskImpl *next_task = nullptr;

        for (uint32_t i = 0; i < task_list_.NumTasks(); i++)
        {
            TaskImpl *task = &task_list_[i];

            //  If the current task is a Zombie, remove it from the list.
            //      We do this in a while loop as the replacement *could* also be a Zombie.

            while (task->State() == Task::ExecutionState::ZOMBIE)
            {
                //  Remove takes the last task in the list and places it into the slot to be removed

                task_list_.RemoveTaskByIndex(i);

                if (i >= task_list_.NumTasks())
                {
                    break;
                }

                task = &task_list_[i];
            }

            if (i < task_list_.NumTasks())
            {
                if (((task->State() == Task::ExecutionState::RUNNING) ||
                     (task->State() == Task::ExecutionState::RUNNABLE_WAITING)))
                {
                    if (task->counter_ > max_counter)
                    {
                        max_counter = task->counter_;
                        next_task = task;
                    }
                }
            }
        }

        //  If we do not have a task counter with a value > 0, update the counters

        if (max_counter <= 0)
        {
            max_counter = -1;
            next_task = nullptr;

            for (uint32_t i = 0; i < task_list_.NumTasks(); i++)
            {
                TaskImpl *task = &task_list_[i];

                task->counter_ = minstd::max((task->counter_ >> 1) + task->priority_, (long)0);

                if (((task->State() == Task::ExecutionState::RUNNING) ||
                     (task->State() == Task::ExecutionState::RUNNABLE_WAITING)))
                {
                    if (task->counter_ > max_counter)
                    {
                        max_counter = task->counter_;
                        next_task = task;
                    }
                }
            }
        }

        if (next_task == nullptr)
        {
            return TaskImpl::GetTask();
        }

        //  Return the task with the highest counter value

        return *next_task;
    }

    void TaskExecutionContext::SwitchTasks()
    {
        //  Be very care with the order of operations here - we are in a critical section.
        //      Do not allow interrupts to occur in this code as we are manipulating the task list
        //      and it is not thread re-entrant.  Do not block or take locks anywhere in this code.
        //
        //  The KernelExit method will re-enable interrupts at the eret instruction.
        //
        //  Even if we had a lock-free task list, there is still some risk of taking an interrupt while
        //      switching contexts.  Best to just have interrupts disabled for the duration of the switch.
        //      Therefore - make sure the switch is fast.

        time_point<nanoseconds> switch_start = PhysicalTimer::Now();

        TaskImpl *const prev = &TaskImpl::GetTask();

        prev->switched_out_last_ = switch_start;

        //  Update the task task runtime

        if (prev->switched_in_last_.time_since_epoch().count() != 0)
        {
            prev->runtime_ += duration_cast<microseconds>(switch_start - prev->switched_in_last_);
        }

        //  First, groom the task list.

        if (inter_context_message_queue_.HasMessages())
        {
            ServiceMessages();
        }

        //  Decrement the task counter and if it is still > 0 or is not premeptaable
        //      then return without switching tasks.

        prev->counter_ = minstd::max(prev->counter_ - 1, (long)0);

        if ((prev->counter_ > 0) || (prev->preempt_count_ > 0))
        {
            prev->timeslices_granted_++; //  Bump the timeslice count
            prev->switched_in_last_ = PhysicalTimer::Now();
            return;
        }

        //  Find the next task to run, we could get the same task back though.

        TaskImpl *next = &FindNextTask();

        next->timeslices_granted_++;

        if (prev == next)
        {
            return;
        }

        //  Switch the tasks

        next->state_ = Task::ExecutionState::RUNNING;

        if (prev->state_ != Task::ExecutionState::ZOMBIE)
        {
            prev->state_ = Task::ExecutionState::RUNNABLE_WAITING;
        }

        next->switched_in_last_ = PhysicalTimer::Now();

        SwitchCPUState(&(prev->cpu_state_), &(next->cpu_state_));
    }
} // namespace task
