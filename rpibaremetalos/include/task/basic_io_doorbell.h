// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <atomic>

#include "task/tasks.h"

namespace task
{
    //  Wakes one waiting task when a device has something for it.  Ring() is lock-free and safe from an ISR on any
    //      core; Wait() is for task context.  Rings that land while nobody waits are remembered, and any number of
    //      them collapse into one, so the waiter drains its data source completely after each wake.
    //
    //      while (!ring.pop_front(c)) { doorbell.Wait(WaitReason::DEVICE); }      //  reader
    //      ring.push_back(c);  doorbell.Ring();                                     //  ISR
    //
    //  One waiter at a time: a second concurrent waiter is fatal.  The waiter must outlive any Ring() that may
    //      still find it.  Policy is as for BasicWaitQueue.

    template <typename Policy>
    class BasicIODoorbell
    {
    private:
        //  word_ holds IDLE, RUNG, or the address of the waiting task (aligned, so never IDLE or RUNG).
        static constexpr uintptr_t IDLE = 0;
        static constexpr uintptr_t RUNG = 1;

        using Task = typename Policy::Task;

    public:
        BasicIODoorbell() = default;

        BasicIODoorbell(const BasicIODoorbell &) = delete;
        BasicIODoorbell(BasicIODoorbell &&) = delete;

        BasicIODoorbell &operator=(const BasicIODoorbell &) = delete;
        BasicIODoorbell &operator=(BasicIODoorbell &&) = delete;

        //  May return spuriously: callers re-check their data source.

        void Wait(WaitReason reason)
        {
            WaitUntil(reason, 0);
        }

        //  False if the timeout passed with no ring.

        bool Wait(WaitReason reason, microseconds timeout)
        {
            return WaitUntil(reason, Policy::DeadlineAfter(timeout));
        }

        void Ring()
        {
            ring_ticks_.store(Policy::NowTicks(), minstd::memory_order_relaxed);

            const uintptr_t previous = word_.exchange(RUNG, minstd::memory_order_acq_rel);

            if (previous > RUNG)
            {
                Policy::Wake(*reinterpret_cast<Task *>(previous), WaitStatus::WOKEN);
            }
        }

        //  Ticks from the Ring() that ended the last Wait() to the waiter running again; 0 if that Wait() did not
        //      block or ended without a ring.

        uint64_t LastWakeLatencyTicks() const
        {
            return last_wake_latency_ticks_;
        }

    private:
        minstd::atomic<uintptr_t> word_{IDLE};
        minstd::atomic<uint64_t> ring_ticks_{0};

        uint64_t last_wake_latency_ticks_ = 0; //  waiter only

        bool WaitUntil(WaitReason reason, uint64_t deadline_ticks)
        {
            Task &self = Policy::Current();

            typename Policy::IRQGuard irqs;

            last_wake_latency_ticks_ = 0;

            uintptr_t expected = RUNG;

            if (word_.compare_exchange_strong(expected, IDLE, minstd::memory_order_acq_rel, minstd::memory_order_acquire))
            {
                return true;
            }

            if (expected != IDLE)
            {
                Policy::Fatal("IODoorbell has a second waiter");
            }

            Policy::PrepareToBlock(self, reason, deadline_ticks);

            expected = IDLE;

            if (!word_.compare_exchange_strong(expected, reinterpret_cast<uintptr_t>(&self), minstd::memory_order_acq_rel, minstd::memory_order_acquire))
            {
                if (expected != RUNG)
                {
                    Policy::Fatal("IODoorbell has a second waiter");
                }

                //  Rung since the first check.  End the wait just begun; CommitBlock() then returns at once.

                Policy::Wake(self, WaitStatus::WOKEN);
            }

            const WaitStatus how = Policy::CommitBlock(self);

            //  Clears our own address after a timeout or a wake from elsewhere, or consumes the RUNG.  A ring that
            //      races a timeout still counts: its data is there to read.

            const bool rung = (word_.exchange(IDLE, minstd::memory_order_acq_rel) == RUNG);

            if (rung)
            {
                last_wake_latency_ticks_ = Policy::NowTicks() - ring_ticks_.load(minstd::memory_order_relaxed);
            }

            return rung || (how == WaitStatus::WOKEN);
        }
    };
} // namespace task
