/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "devices/physical_timer.h"


void PhysicalTimer::Wait(microseconds delay)
{
    uint64_t counter_frequency;

    //  Get the timer frequency

    asm volatile("mrs %0, cntfrq_el0" : "=r"(counter_frequency));

    //  Determine the wait in terms of timer 'ticks'

    uint64_t count_to_elapse = ( delay.count() * counter_frequency ) / 1000000;

    //  Read the current timer value

    uint64_t start_count;

    asm volatile("mrs %0, cntpct_el0" : "=r"(start_count));

    //  Loop until the elapsed time since the start is greater than the required elapsed time
    
    uint64_t current_count;

    do
    {
        asm volatile("mrs %0, cntpct_el0" : "=r"(current_count));

        for (unsigned int i = 0; i < 10; i++)
        {
            asm volatile("nop");
        }
    } while ((current_count - start_count) < count_to_elapse);
}
