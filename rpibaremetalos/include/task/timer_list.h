// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace task
{
    //  An intrusive list of nodes sorted by wake_deadline_ticks_, earliest first, linked through each node's
    //      timer_next_ / timer_prev_.  One per core, touched only by that core with IRQs disabled, so it takes no
    //      lock.  A node is on at most one TimerList at a time.

    template <typename Node>
    class TimerList
    {
    public:
        TimerList() = default;

        TimerList(const TimerList &) = delete;
        TimerList(TimerList &&) = delete;

        TimerList &operator=(const TimerList &) = delete;
        TimerList &operator=(TimerList &&) = delete;

        bool Empty() const
        {
            return head_ == nullptr;
        }

        bool Contains(const Node &node) const
        {
            return (node.timer_prev_ != nullptr) || (head_ == &node);
        }

        //  O(n) in this core's timers.  A node is placed after any with an equal deadline.

        void Insert(Node &node)
        {
            Node *previous = nullptr;
            Node *current = head_;

            while ((current != nullptr) && (current->wake_deadline_ticks_ <= node.wake_deadline_ticks_))
            {
                previous = current;
                current = current->timer_next_;
            }

            node.timer_prev_ = previous;
            node.timer_next_ = current;

            if (current != nullptr)
            {
                current->timer_prev_ = &node;
            }

            if (previous != nullptr)
            {
                previous->timer_next_ = &node;
            }
            else
            {
                head_ = &node;
            }
        }

        //  A node that is not on the list is left alone.

        void Remove(Node &node)
        {
            if (!Contains(node))
            {
                return;
            }

            if (node.timer_prev_ != nullptr)
            {
                node.timer_prev_->timer_next_ = node.timer_next_;
            }
            else
            {
                head_ = node.timer_next_;
            }

            if (node.timer_next_ != nullptr)
            {
                node.timer_next_->timer_prev_ = node.timer_prev_;
            }

            node.timer_next_ = nullptr;
            node.timer_prev_ = nullptr;
        }

        //  0 when the list is empty.

        uint64_t EarliestDeadline() const
        {
            return (head_ != nullptr) ? head_->wake_deadline_ticks_ : 0;
        }

        //  Unlinks every node whose deadline is at or before now_ticks, earliest first, and passes each to
        //      on_expired after unlinking it.

        template <typename OnExpired>
        void ExpireUpTo(uint64_t now_ticks, OnExpired &&on_expired)
        {
            while ((head_ != nullptr) && (head_->wake_deadline_ticks_ <= now_ticks))
            {
                Node &expired = *head_;

                Remove(expired);
                on_expired(expired);
            }
        }

    private:
        Node *head_ = nullptr;
    };
} // namespace task
