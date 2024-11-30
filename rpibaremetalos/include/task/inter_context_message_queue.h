// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/task_impl.h"

#include <lockfree/spsc_queue>

#include "asm_utility.h"

namespace task
{
    //
    //  The inter context message class should be concrete as the lockless queue requires a copyable type
    //      and it contains all of the storage for the element so there is no need for dynamic allocation of messages.
    //      Allocating any part of the message on the heap can lead to deadlocks if the allocator takes a lock
    //      as the message is processed and if dynamic would be released inside the task switching code which
    //      is inside the disabled IRQ context.
    //
    //  Bottom line - no locks should *ever* be taken in the task switching code.
    //

    class InterContextMessage
    {
    public:
        enum class MessageType : uint32_t
        {
            EMPTY,
            ADD_TASK,
            SURRENDER_TASK,
        };

        InterContextMessage()
            : type_(MessageType::EMPTY),
              task_(nullptr)
        {
        }

        InterContextMessage(MessageType type,
                            TaskImpl &task)
            : type_(type),
              task_(&task) {
              };

        InterContextMessage(const InterContextMessage &message_to_copy)
            : type_(message_to_copy.type_),
              task_(message_to_copy.task_)
        {
        }

        InterContextMessage(InterContextMessage &&message_to_move)
            : type_(message_to_move.type_),
              task_(message_to_move.task_)
        {
        }

        ~InterContextMessage() = default;

        InterContextMessage &operator=(const InterContextMessage &message_to_copy)
        {
            type_ = message_to_copy.type_;
            task_ = message_to_copy.task_;

            return *this;
        }

        InterContextMessage &operator=(InterContextMessage &&message_to_move)
        {
            type_ = message_to_move.type_;
            task_ = message_to_move.task_;

            return *this;
        }

        MessageType Type() const
        {
            return type_;
        }

        TaskImpl &Task()
        {
            return *task_;
        }

    private:

        MessageType type_;

        TaskImpl *task_;
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

        void QueueMessage(InterContextMessage &message)
        {
            while (!queue_.push_back(message))
            {
                //  Spin until we can add the message
                LogWarning("InterContextMessageQueue::SendMessage - Queue full, spinning\n");
                CPUTicksDelay(1000);
            }
        }

        bool PopMessage(InterContextMessage &message)
        {
            return queue_.pop_front(message);
        }

    private:
        using TaskQueue = minstd::spsc_queue<InterContextMessage>;
        using TaskQueueAllocator = minstd::allocator<TaskQueue::value_type>;
        using TaskQueueHeapAllocator = minstd::heap_allocator<TaskQueue::value_type>;

        //  The queue can go into the static heap as it is a fixed size

        TaskQueueHeapAllocator queue_heap_allocator_{__os_static_heap};
        TaskQueue queue_{queue_heap_allocator_, MAX_QUEUE_LENGTH};
    };
} // namespace task
