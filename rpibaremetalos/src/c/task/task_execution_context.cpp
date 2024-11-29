// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_execution_context.h"

#include "devices/physical_timer.h"
#include "devices/log.h"

extern "C" void SwitchCPUState(task::TaskImpl::TaskContextCPUState *prev, task::TaskImpl::TaskContextCPUState *next);

namespace task
{
    void TaskExecutionContext::ServiceMessages()
    {
        //  Process any queued messages

        minstd::unique_ptr<InterContextMessage> message;

        while( inter_context_message_queue_.PopMessage(message) )
        {
            switch( message->Type() )
            {
                case InterContextMessage::MessageType::ADD_TASK:
                {
                    //  Add the task to the task list

                    AddTaskMessage &add_task_message = static_cast<AddTaskMessage &>(*message);

                    task_list_.push_back(minstd::move(add_task_message.Task()));

                    break;
                }

                case InterContextMessage::MessageType::SURRENDER_TASK:
                {
                    //  TODO Implement
                    break;
                }
            }
        }
    }

    
    TaskImpl &TaskExecutionContext::FindNextTask(void)
    {
        long max_counter = -1;
        uint32_t num_runnables = 0;
        TaskList::iterator next_task = task_list_.end();

        while (true)
        {
            max_counter = -1;
            next_task = task_list_.end();
            num_runnables = 0;

            for (auto itr = task_list_.begin(); itr != task_list_.end(); ++itr)
            {
                TaskImpl &task = *itr;

                if(task.State() == Task::ExecutionState::ZOMBIE)
                {
                    continue;
                }

                if (((task.State() == Task::ExecutionState::RUNNING) ||
                     (task.State() == Task::ExecutionState::RUNNABLE_WAITING)))
                {
                    num_runnables++;

                    if (task.counter_ > max_counter)
                    {
                        max_counter = task.counter_;
                        next_task = itr;
                    }
                }
            }

            //  If we do not have a task counter with a value > 0, update the counters

            if ((max_counter) || (num_runnables == 0))
            {
                break;
            }

            for (auto itr = task_list_.begin(); itr != task_list_.end(); ++itr)
            {
                TaskImpl &task = *itr;

                if(task.State() == Task::ExecutionState::ZOMBIE)
                {
                    continue;
                }

                task.counter_ = (task.counter_ >> 1) + task.priority_;
            }
        }

        if (next_task == task_list_.end())
        {
            return TaskImpl::GetTask();
        }

        //  Return the task with the highest counter value

        return *next_task;
    }

    void TaskExecutionContext::SwitchTasks()
    {
        DisableIRQ();

        //  First, groom the task list.

        if( inter_context_message_queue_.HasMessages() )
        {
            ServiceMessages();
        }

        //  Decrement the task counter and if it is still > 0 or is not premeptaable
        //      then return without switching tasks.

        --TaskImpl::GetTask().counter_;

        if (TaskImpl::GetTask().counter_ > 0 || TaskImpl::GetTask().preempt_count_ > 0)
        {
            EnableIRQ();
            return;
        }

        TaskImpl::GetTask().counter_ = 0;

//        EnableIRQ();

        TaskImpl *const prev = &TaskImpl::GetTask();
        TaskImpl *next = nullptr;

        {
//            NonPreemptableSection non_preemptable_section;

            next = &FindNextTask();

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

//            printf("Core: %d    Switching from %s to %s\n", GetCoreID(), prev->Name().c_str(), next->Name().c_str());
        }

        EnableIRQ();

        SwitchCPUState(&(prev->cpu_state_), &(next->cpu_state_));

//        DisableIRQ();

        if (TaskImpl::GetTask().State() == Task::ExecutionState::ZOMBIE)
        {
            LogError("Core: %d    Switched to Zombie Task on return\n", GetCoreID());
        }
    }
} // namespace task
