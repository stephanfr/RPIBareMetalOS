// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "task/tasks.h"

namespace task
{
    //  Tasks blocked until a condition holds, linked through each task's wait_queue_next_ / wait_queue_prev_.  A
    //      task waits on at most one queue at a time.  Wakes may come from any core or an ISR; waits only from task context.
    //
    //      queue.Wait([&]() { return data_ready; }, WaitReason::EVENT);       //  waiter
    //      data_ready = true;  queue.WakeOne();                               //  waker
    //
    //  The condition runs under the queue's spinlock with IRQs off: keep it to a few loads.  A waker that changes
    //      the condition before calling WakeOne() / WakeAll() cannot be missed.
    //
    //  Destroying a queue that a task is still waiting on, or still leaving a wait on, is fatal.
    //
    //  Policy supplies the scheduler: the Task type, a Lock, an IRQGuard, Current(), PrepareToBlock(),
    //      CommitBlock(), Wake(), NowTicks(), DeadlineAfter(), PreemptionPoint() and a [[noreturn]] Fatal(message).
    //      The kernel's is KernelWaitPolicy (wait_queue.h); the host tests supply a fake.

    template <typename Policy>
    class BasicWaitQueue
    {
    public:
        using Task = typename Policy::Task;

        typedef enum class Outcome : uint32_t
        {
            CONDITION_MET = 0,
            WOKEN, //  but the condition did not hold when re-checked
            TIMED_OUT,
            INTERRUPTED, //  only for an interruptible reason (IsInterruptible())
        } Outcome;

        typedef enum class Position : uint32_t
        {
            TAIL = 0,
            HEAD,
        } Position;

        BasicWaitQueue() = default;

        ~BasicWaitQueue()
        {
            typename Policy::IRQGuard irqs;

            lock_.Lock();

            const bool in_use = (waiters_inside_ != 0);

            lock_.Unlock();

            if (in_use)
            {
                Policy::Fatal("WaitQueue destroyed while a task waits on it");
            }
        }

        BasicWaitQueue(const BasicWaitQueue &) = delete;
        BasicWaitQueue(BasicWaitQueue &&) = delete;

        BasicWaitQueue &operator=(const BasicWaitQueue &) = delete;
        BasicWaitQueue &operator=(BasicWaitQueue &&) = delete;

        //  Returns with the condition unmet only if the wait was interrupted, which needs an interruptible reason.

        template <typename Condition>
        void Wait(Condition &&condition, WaitReason reason)
        {
            for (;;)
            {
                const Outcome outcome = WaitOnce(condition, reason, 0, Position::TAIL);

                if ((outcome == Outcome::CONDITION_MET) || (outcome == Outcome::INTERRUPTED))
                {
                    return;
                }
            }
        }

        //  False if timeout passes first, or the wait was interrupted.

        template <typename Condition>
        bool Wait(Condition &&condition, WaitReason reason, microseconds timeout)
        {
            const uint64_t deadline_ticks = Policy::DeadlineAfter(timeout);

            for (;;)
            {
                switch (WaitOnce(condition, reason, deadline_ticks, Position::TAIL))
                {
                case Outcome::CONDITION_MET:
                    return true;

                case Outcome::TIMED_OUT:
                case Outcome::INTERRUPTED:
                    return false;

                default:
                    break;
                }
            }
        }

        //  One step of Wait(), for callers that act between wakes.  Returns without blocking if the condition holds
        //      or deadline_ticks (0 for none) has passed.  After blocking, the condition is checked again, so
        //      CONDITION_MET wins over WOKEN or TIMED_OUT.

        template <typename Condition>
        Outcome WaitOnce(Condition &&condition, WaitReason reason, uint64_t deadline_ticks, Position position)
        {
            Task &self = Policy::Current();

            typename Policy::IRQGuard irqs;

            lock_.Lock();

            if (condition())
            {
                lock_.Unlock();
                return Outcome::CONDITION_MET;
            }

            if ((deadline_ticks != 0) && (Policy::NowTicks() >= deadline_ticks))
            {
                lock_.Unlock();
                return Outcome::TIMED_OUT;
            }

            //  WAITING before linking, so a waker never finds the task linked but not yet waiting.

            Policy::PrepareToBlock(self, reason, deadline_ticks);
            Link(self, position);
            waiters_inside_++;

            lock_.Unlock();

            const WaitStatus how = Policy::CommitBlock(self);

            //  WakeOne() and WakeAll() unlink before they wake, but a timeout, or a wake addressed to the task
            //      rather than to this queue, leaves it linked.

            lock_.Lock();

            Unlink(self);
            waiters_inside_--;

            const bool condition_met = condition();

            lock_.Unlock();

            if (condition_met)
            {
                return Outcome::CONDITION_MET;
            }

            if (how == WaitStatus::TIMED_OUT)
            {
                return Outcome::TIMED_OUT;
            }

            //  A kill that raced a change of wait may address a wait that isn't interruptible: that is a spurious wake.

            return ((how == WaitStatus::INTERRUPTED) && IsInterruptible(reason)) ? Outcome::INTERRUPTED : Outcome::WOKEN;
        }

        //  The number of tasks woken, 0 or 1.  Waiters whose wait already ended are skipped.

        uint32_t WakeOne()
        {
            return WakeOne([](Task *) {});
        }

        //  As WakeOne(), but on_pop(task) runs under the queue lock before each wake it tries, and on_pop(nullptr)
        //      if none succeeds, so the caller can hand the woken task something no other task can take first.

        template <typename OnPop>
        uint32_t WakeOne(OnPop &&on_pop)
        {
            uint32_t woken = 0;

            {
                typename Policy::IRQGuard irqs;

                lock_.Lock();

                while ((woken == 0) && (head_ != nullptr))
                {
                    Task &task = *head_;

                    Unlink(task);
                    on_pop(&task);

                    woken = Policy::Wake(task, WaitStatus::WOKEN) ? 1 : 0;
                }

                if (woken == 0)
                {
                    on_pop(nullptr);
                }

                lock_.Unlock();
            }

            Policy::PreemptionPoint();

            return woken;
        }

        uint32_t WakeAll()
        {
            uint32_t woken = 0;

            {
                typename Policy::IRQGuard irqs;

                lock_.Lock();

                while (head_ != nullptr)
                {
                    Task &task = *head_;

                    Unlink(task);

                    woken += Policy::Wake(task, WaitStatus::WOKEN) ? 1 : 0;
                }

                lock_.Unlock();
            }

            Policy::PreemptionPoint();

            return woken;
        }

    private:
        typename Policy::Lock lock_;

        Task *head_ = nullptr;
        Task *tail_ = nullptr;

        //  Under lock_: tasks between linking themselves and their final unlink.  A woken task is unlinked by its
        //      waker but still has to take lock_ again, so an empty list alone does not make the queue safe to
        //      destroy.
        uint32_t waiters_inside_ = 0;

        //  Both under lock_.

        void Link(Task &task, Position position)
        {
            if (position == Position::HEAD)
            {
                task.wait_queue_prev_ = nullptr;
                task.wait_queue_next_ = head_;

                if (head_ != nullptr)
                {
                    head_->wait_queue_prev_ = &task;
                }
                else
                {
                    tail_ = &task;
                }

                head_ = &task;
                return;
            }

            task.wait_queue_next_ = nullptr;
            task.wait_queue_prev_ = tail_;

            if (tail_ != nullptr)
            {
                tail_->wait_queue_next_ = &task;
            }
            else
            {
                head_ = &task;
            }

            tail_ = &task;
        }

        //  A task that is not linked here is left alone.

        void Unlink(Task &task)
        {
            if ((task.wait_queue_prev_ == nullptr) && (head_ != &task))
            {
                return;
            }

            if (task.wait_queue_prev_ != nullptr)
            {
                task.wait_queue_prev_->wait_queue_next_ = task.wait_queue_next_;
            }
            else
            {
                head_ = task.wait_queue_next_;
            }

            if (task.wait_queue_next_ != nullptr)
            {
                task.wait_queue_next_->wait_queue_prev_ = task.wait_queue_prev_;
            }
            else
            {
                tail_ = task.wait_queue_prev_;
            }

            task.wait_queue_next_ = nullptr;
            task.wait_queue_prev_ = nullptr;
        }
    };
} // namespace task
