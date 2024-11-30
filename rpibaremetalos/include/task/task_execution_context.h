// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/inter_context_message_queue.h"

namespace task
{
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

        void SwitchTasks();

    private:

        PerCoreTaskList task_list_;

        //  Messaging queue for inter-context messages can go into the static heap

        InterContextMessageQueue inter_context_message_queue_;

        //
        //  Methods
        //
        
        void ServiceMessages();

        TaskImpl &FindNextTask(void);
    };
} // namespace task
