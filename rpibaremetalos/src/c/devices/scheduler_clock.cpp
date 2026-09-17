// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/scheduler_clock.h"

#include "os_config.h"
#include "heaps.h"

#include "asm_utility.h"

#include "devices/log.h"
#include "devices/system_timer.h"
#include "platform/platform_info.h"

namespace
{
    //
    //  RPi3 -- the BCM system timer compare 1 path.
    //

    class SystemTimerClock final : public SchedulerClock
    {
    public:
        Interrupts TickInterrupt() const override
        {
            return Interrupts::SYSTEM_TIMER_1;
        }

        void Start(microseconds period) override
        {
            GetSystemTimer().StartRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1, period);
        }

        void Rearm() override
        {
            GetSystemTimer().RescheduleRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1);
        }

        void Cancel() override
        {
            GetSystemTimer().CancelRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1);
        }
    };

    //
    //  RPi4/RPi5 -- the ARM generic timer, CNTP_*_EL0, delivered as GIC PPI INTID 30.
    //

    class GenericTimerClock final : public SchedulerClock
    {
    private:
        static constexpr uint64_t CNTP_CTL_ENABLE = 0x1;
        static constexpr uint64_t CNTP_CTL_IMASK = 0x2;

    public:
        Interrupts TickInterrupt() const override
        {
            return Interrupts::ARM_GENERIC_TIMER;
        }

        void Start(microseconds period) override
        {
            if (period == microseconds::zero())
            {
                return;
            }

            period_in_ticks_ = TicksForPeriod(period);
            running_ = true;

            next_deadline_ = CurrentTicks() + period_in_ticks_;

            SetCompareValue(next_deadline_);
            SetControl(CNTP_CTL_ENABLE); //  IMASK clear -- the timer may now interrupt
        }

        void Rearm() override
        {
            if (!running_)
            {
                //  Mask the timer so a stale deadline cannot re-enter.

                SetControl(CNTP_CTL_ENABLE | CNTP_CTL_IMASK);
                return;
            }

            next_deadline_ += period_in_ticks_;

            //  If we missed the next deadline, or are within a tenth of a period of missing it,
            //      warn and re-base on now -- mirrors SystemTimerImpl::RescheduleRecurringInterrupt.

            uint64_t now = CurrentTicks();

            if (next_deadline_ < (now + (period_in_ticks_ / 10)))
            {
                LogWarning("******Missed interrupt******\n");

                next_deadline_ = now + period_in_ticks_;
            }

            SetCompareValue(next_deadline_);
        }

        void Cancel() override
        {
            running_ = false;

            SetControl(CNTP_CTL_ENABLE | CNTP_CTL_IMASK);
        }

    private:
        bool running_ = false;
        uint64_t period_in_ticks_ = 0;
        uint64_t next_deadline_ = 0;

        static uint64_t CurrentTicks()
        {
            uint64_t count;

            asm volatile("mrs %0, cntpct_el0" : "=r"(count));

            return count;
        }

        static uint64_t CounterFrequency()
        {
            uint64_t frequency;

            asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));

            return frequency;
        }

        static uint64_t TicksForPeriod(microseconds period)
        {
            uint64_t ticks = (CounterFrequency() * static_cast<uint64_t>(period.count())) / MICROSECONDS_PER_SECOND;

            return ticks > 0 ? ticks : 1;
        }

        static void SetCompareValue(uint64_t deadline)
        {
            asm volatile("msr cntp_cval_el0, %0" ::"r"(deadline));

            INSTRUCTION_CACHE_BARRIER;
        }

        static void SetControl(uint64_t control)
        {
            asm volatile("msr cntp_ctl_el0, %0" ::"r"(control));

            INSTRUCTION_CACHE_BARRIER;
        }
    };
}

static SchedulerClock *scheduler_clock_ = nullptr;

SchedulerClock &GetSchedulerClock()
{
    if (scheduler_clock_ == nullptr)
    {
        //  The tick source is a descriptor field -- no board test here.

        switch (GetPlatformInfo().GetSchedulerClockSource())
        {
        case SchedulerClockSource::BCM_SYSTEM_TIMER_COMPARE_1:
            scheduler_clock_ = static_new<SystemTimerClock>();
            break;

        case SchedulerClockSource::ARM_GENERIC_TIMER:
            scheduler_clock_ = static_new<GenericTimerClock>();
            break;
        }
    }

    return *scheduler_clock_;
}
