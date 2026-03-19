// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"
#include "asm_utility.h"

class PhysicalTimer
{
public:
    PhysicalTimer() = delete;
    PhysicalTimer(const PhysicalTimer &) = delete;
    PhysicalTimer(PhysicalTimer &&) = delete;

    PhysicalTimer &operator=(const PhysicalTimer &) = delete;
    PhysicalTimer &operator=(PhysicalTimer &&) = delete;

    static void Wait(microseconds delay);

    static uint64_t CurrentTicks(void)
    {
        unsigned long long current_count;

        asm volatile("mrs %0, cntpct_el0" : "=r"(current_count));

        return current_count;
    }

    static minstd::chrono::time_point<minstd::chrono::nanoseconds> Now(void)
    {
        uint64_t counter_frequency;
        uint64_t current_count;

        //  Get the timer frequency

        asm volatile("mrs %0, cntfrq_el0" : "=r"(counter_frequency));
        asm volatile("mrs %0, cntpct_el0" : "=r"(current_count));

        INSTRUCTION_CACHE_BARRIER;

        return minstd::chrono::time_point<minstd::chrono::nanoseconds>( minstd::chrono::nanoseconds((current_count * 1000000000UL) / counter_frequency));
    }
};