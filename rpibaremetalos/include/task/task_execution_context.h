// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/inter_context_message_queue.h"
#include "task/timer_list.h"

namespace task
{
    //  Why SwitchTasks() was called.  Only TICK charges the running task's timeslice; YIELD gives up the rest
    //      of it; BLOCK keeps it.

    typedef enum class SwitchReason : uint32_t
    {
        TICK = 0,
        PREEMPT,
        YIELD,
        BLOCK,
    } SwitchReason;

    class PerCoreTaskList
    {
        public :

        PerCoreTaskList() = default;
        PerCoreTaskList(const PerCoreTaskList &) = delete;
        PerCoreTaskList(PerCoreTaskList &&) = delete;
        
        ~PerCoreTaskList() = default;

        PerCoreTaskList &operator=(const PerCoreTaskList &) = delete;
        PerCoreTaskList &operator=(PerCoreTaskList &&) = delete;

        uint32_t NumTasks() const
        {
            return num_tasks_;
        }

        TaskImpl &operator[](uint32_t index)
        {
            return *task_list_[index];
        }

        bool AddTask(TaskImpl &task)
        {
            //  If the task list is full, return false
            
            if( num_tasks_ >= MAX_ACTIVE_TASKS_PER_CORE - 1 )
            {
                return false;
            }
            
            task_list_[num_tasks_++] = &task;
            return true;
        }

        void RemoveTaskByIndex(uint32_t index)
        {
            if( index >= num_tasks_ )
            {
                return;
            }

            if( index == num_tasks_ - 1 )
            {
                num_tasks_--;
                return;
            }

            task_list_[index] = task_list_[--num_tasks_];
        }

        private :

        uint32_t num_tasks_ = 0;
        minstd::array<TaskImpl*, MAX_ACTIVE_TASKS_PER_CORE> task_list_;
    };

    class TaskExecutionContext
    {
    public:
        TaskExecutionContext() = default;

        ~TaskExecutionContext() = default;

        void QueueMessage(InterContextMessage &message)
        {
            inter_context_message_queue_.QueueMessage(message);
        }

        //  Call with IRQs disabled; returns with them disabled, whether or not a switch happened.  May return
        //      only when this task is next switched in.

        void SwitchTasks(SwitchReason reason);

        //  This core's tasks waiting with a deadline.  Owning core only, IRQs disabled.

        void AddWaitDeadline(TaskImpl &task)
        {
            wait_deadlines_.Insert(task);
        }

        //  A task with no deadline listed is left alone.

        void RemoveWaitDeadline(TaskImpl &task)
        {
            wait_deadlines_.Remove(task);
        }

    private:

        PerCoreTaskList task_list_;

        TimerList<TaskImpl> wait_deadlines_;

        //  Messaging queue for inter-context messages can go into the static heap

        InterContextMessageQueue inter_context_message_queue_;

        //
        //  Methods
        //
        
        void ServiceMessages();

        void ExpireWaitDeadlines();

        void MakeRunnableIfWaitEnded(TaskImpl &task);
        void MarkRunning(TaskImpl &task);
        void MarkSwitchedOut(TaskImpl &task);

        TaskImpl &FindNextTask(void);
    };
} // namespace task
