// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"
#include "asm_utility.h"
#include "utility/tick_conversion.h"

class PhysicalTimer
{
public:
    PhysicalTimer() = delete;
    PhysicalTimer(const PhysicalTimer &) = delete;
    PhysicalTimer(PhysicalTimer &&) = delete;

    PhysicalTimer &operator=(const PhysicalTimer &) = delete;
    PhysicalTimer &operator=(PhysicalTimer &&) = delete;

    static void Wait(microseconds delay);

    //  The isb keeps the counter read from being speculated ahead of the code that precedes it.

    static uint64_t CurrentTicks(void)
    {
        unsigned long long current_count;

        asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(current_count) : : "memory");

        return current_count;
    }

    //  The system counter value duration from now, rounded up so a wait for it never ends early.

    static uint64_t DeadlineTicksAfter(minstd::chrono::nanoseconds duration)
    {
        const uint64_t nanoseconds = (duration.count() > 0) ? static_cast<uint64_t>(duration.count()) : 0;

        return CurrentTicks() + utility::NanosecondsToTicks(nanoseconds, GetCounterFrequency());
    }

    static minstd::chrono::time_point<minstd::chrono::nanoseconds> Now(void)
    {
        return minstd::chrono::time_point<minstd::chrono::nanoseconds>(
            minstd::chrono::nanoseconds(utility::TicksToNanoseconds(CurrentTicks(), GetCounterFrequency())));
    }

    //  Time since the physical counter started at SoC reset, so it includes the firmware's boot time.

    static minstd::chrono::nanoseconds UptimeSinceBoot(void)
    {
        return Now().time_since_epoch();
    }
};