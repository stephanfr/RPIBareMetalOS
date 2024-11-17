// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/task_impl.h"

#include <lockfree/spsc_queue>

#include "heaps.h"
#include "asm_utility.h"

namespace task
{
    class InterContextMessage
    {
    public:

        enum class MessageType
        {
            ADD_TASK,
            SURRENDER_TASK,
        };

        InterContextMessage() = default;

        virtual ~InterContextMessage() = default;

        virtual MessageType Type() const = 0;
    };

    class AddTaskMessage : public InterContextMessage
    {   
    public:
        AddTaskMessage(TaskImpl &task)
            : task_(task)
        {
        }

        virtual MessageType Type() const override
        {
            return MessageType::ADD_TASK;
        }

        TaskImpl &Task() const
        {
            return task_;
        }

        private :

        TaskImpl &task_;
    };

    class InterContextMessageQueue
    {
    public:
        static constexpr size_t MAX_QUEUE_LENGTH = 64;

        InterContextMessageQueue() = default;

        bool HasMessages() const
        {
            return !queue_.empty();
        }

        void QueueMessage(minstd::unique_ptr<InterContextMessage> &message)
        {
            while (!queue_.push_back(message))
            {
                //  Spin until we can add the message
                LogWarning("InterContextMessageQueue::SendMessage - Queue full, spinning\n");
                CPUTicksDelay(1000);
            }
        }

        bool PopMessage(minstd::unique_ptr<InterContextMessage> &message)
        {
            return queue_.pop_front(message);
        }

    private:
        using TaskQueue = minstd::spsc_queue<minstd::unique_ptr<InterContextMessage>>;
        using TaskQueueAllocator = minstd::allocator<TaskQueue::value_type>;
        using TaskQueueHeapAllocator = minstd::heap_allocator<TaskQueue::value_type>;

        //  The queue can go into the static heap as it is a fixed size

        TaskQueueHeapAllocator queue_heap_allocator_{__os_static_heap};
        TaskQueue queue_{queue_heap_allocator_, MAX_QUEUE_LENGTH};
    };
} // namespace task
