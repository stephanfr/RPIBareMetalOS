// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <atomic>

#include "task/basic_wait_queue.h"

namespace task
{
    //  A sleeping lock for task context.  A contended Lock() blocks instead of spinning.
    //      An arriving task may take a just-released lock ahead of the waiters (barging), which keeps it moving.
    //      A waiter that loses that race once sets HANDOFF, and the next Unlock() then passes the lock straight to
    //      the waiter at the head of the queue, so no waiter starves.
    //
    //  Not recursive, and not for ISRs.  Policy is as for BasicWaitQueue, plus InInterrupt(), RecordHandoff() and
    //      a [[noreturn]] Fatal(message).

    template <typename Policy>
    class BasicMutex
    {
    private:
        //  owner_ holds the owning task's address, with HANDOFF in bit 0 (tasks are at least 2-byte aligned).
        static constexpr uintptr_t HANDOFF = 1;

        using WaitQueue = BasicWaitQueue<Policy>;
        using Task = typename Policy::Task;

    public:
        BasicMutex() = default;

        BasicMutex(const BasicMutex &) = delete;
        BasicMutex(BasicMutex &&) = delete;

        BasicMutex &operator=(const BasicMutex &) = delete;
        BasicMutex &operator=(BasicMutex &&) = delete;

        bool TryLock()
        {
            uintptr_t expected = 0;

            return owner_.compare_exchange_strong(expected, Self(), minstd::memory_order_acquire);
        }

        void Lock()
        {
            if (Policy::InInterrupt())
            {
                Policy::Fatal("Mutex::Lock called from an interrupt");
            }

            const uintptr_t self = Self();

            if ((owner_.load(minstd::memory_order_relaxed) & ~HANDOFF) == self)
            {
                Policy::Fatal("Mutex::Lock called by the task that holds it");
            }

            bool woken_before = false;

            for (;;)
            {
                uintptr_t expected = 0;

                if (owner_.compare_exchange_strong(expected, self, minstd::memory_order_acquire))
                {
                    return;
                }

                //  Handed off to us.  Another waiter may already have set HANDOFF on our ownership.

                if ((expected & ~HANDOFF) == self)
                {
                    return;
                }

                //  Lost to a barger after being woken: ask for a handoff.  A compare-exchange, never an OR: if the owner
                //      released in between, setting the bit on 0 would leave a lock nobody owns and nobody can take.

                if (woken_before && ((expected & HANDOFF) == 0) &&
                    !owner_.compare_exchange_strong(expected, expected | HANDOFF, minstd::memory_order_acq_rel))
                {
                    continue;
                }

                waiters_.WaitOnce([this, self]()
                                  {
                                      const uintptr_t owner = owner_.load(minstd::memory_order_acquire);
                                      return (owner == 0) || ((owner & ~HANDOFF) == self);
                                  },
                                  WaitReason::LOCK, 0, woken_before ? WaitQueue::Position::HEAD : WaitQueue::Position::TAIL);

                woken_before = true;
            }
        }

        void Unlock()
        {
            const uintptr_t self = Self();

            uintptr_t expected = self;

            if (owner_.compare_exchange_strong(expected, 0, minstd::memory_order_release))
            {
                waiters_.WakeOne();
                return;
            }

            if (expected != (self | HANDOFF))
            {
                Policy::Fatal("Mutex::Unlock called by a task that does not hold it");
            }

            //  While HANDOFF is set owner_ is never 0, so no barger can take the lock before the head waiter.  With no
            //      waiter left, release it.

            waiters_.WakeOne([this](Task *next)
                             {
                                 owner_.store(reinterpret_cast<uintptr_t>(next), minstd::memory_order_release);

                                 if (next != nullptr)
                                 {
                                     Policy::RecordHandoff();
                                 }
                             });
        }

    private:
        minstd::atomic<uintptr_t> owner_{0};
        WaitQueue waiters_;

        static uintptr_t Self()
        {
            return reinterpret_cast<uintptr_t>(&Policy::Current());
        }
    };
} // namespace task
