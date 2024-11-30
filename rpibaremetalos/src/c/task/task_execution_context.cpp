// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_execution_context.h"

#include "devices/log.h"
#include "devices/physical_timer.h"

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

        while (true)
        {
            max_counter = -1;
            next_task = nullptr;

            for (int32_t i = 0; i < task_list_.NumTasks(); i++)
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

            if (max_counter > 0)
            {
                break;
            }

            for (int32_t i = 0; i < task_list_.NumTasks(); i++)
            {
                TaskImpl &task = task_list_[i];

                task.counter_ = minstd::max((task.counter_ >> 1) + task.priority_, (long)0);
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
        //  The KernelExit method will re-enable interrupts just before the eret instruction.
        //
        //  Even if we had a lock-free task list, there is still some risk of taking an interrupt while
        //      switching contexts.  Best to just disable interrupts for the duration of the switch.
        //      Therefore - make sure the switch is fast.

        DisableIRQ();

        //  First, groom the task list.

        if (inter_context_message_queue_.HasMessages())
        {
            ServiceMessages();
        }

        //  Decrement the task counter and if it is still > 0 or is not premeptaable
        //      then return without switching tasks.

        TaskImpl::GetTask().counter_ = minstd::max(TaskImpl::GetTask().counter_ - 1, (long)0);

        if (TaskImpl::GetTask().counter_ > 0 || TaskImpl::GetTask().preempt_count_ > 0)
        {
            EnableIRQ();
            return;
        }

        TaskImpl *const prev = &TaskImpl::GetTask();
        TaskImpl *next = &FindNextTask();

        if (prev == next)
        {
            EnableIRQ();
            return;
        }

        next->state_ = Task::ExecutionState::RUNNING;

        if (prev->state_ != Task::ExecutionState::ZOMBIE)
        {
            prev->state_ = Task::ExecutionState::RUNNABLE_WAITING;
        }

        prev->switched_out_last_ = PhysicalTimer::CurrentTicks();

        SwitchCPUState(&(prev->cpu_state_), &(next->cpu_state_));

        if (TaskImpl::GetTask().State() == Task::ExecutionState::ZOMBIE)
        {
            LogError("Core: %d    Switched to Zombie Task on return\n", GetCoreID());
        }
    }
} // namespace task
