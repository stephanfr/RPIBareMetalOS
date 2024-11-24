// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/inter_context_message_queue.h"

namespace task
{
    class TaskExecutionContext
    {
    public:
        TaskExecutionContext() = default;

        ~TaskExecutionContext() = default;

        void QueueMessage(minstd::unique_ptr<InterContextMessage> &message)
        {
            inter_context_message_queue_.QueueMessage(message);
        }

        void SwitchTasks();

    private:
        using TaskList = minstd::list<minstd::reference_wrapper<TaskImpl>>;
        using TaskListAllocator = minstd::allocator<TaskList::node_type>;
        using TaskListHeapAllocator = minstd::heap_allocator<TaskList::node_type>;

        //  Per core task lists will go into the dynamic kernel queue as they are dynamically sized

        TaskListHeapAllocator task_list_heap_allocator_{__os_dynamic_heap};
        TaskList task_list_{task_list_heap_allocator_};

        //  Messaging queue for inter-context messages can go into the static heap

        InterContextMessageQueue inter_context_message_queue_;

        //
        //  Methods
        //
        
        void ServiceMessages();

        TaskImpl &FindNextTask(void);
    };
} // namespace task
