// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "os_config.h"

namespace utility
{
    //  Converts counter ticks at frequency Hz to nanoseconds; an unknown (zero) frequency reads as 0.  Whole seconds
    //      and the sub-second remainder are scaled separately so the result stays exact for any span that fits in
    //      64-bit nanoseconds (~584 years), where the naive ticks * 1e9 / frequency overflows after ~5 minutes at
    //      54 MHz.

    constexpr uint64_t TicksToNanoseconds(uint64_t ticks, uint64_t frequency) noexcept
    {
        if (frequency == 0)
        {
            return 0;
        }

        const uint64_t whole_seconds = ticks / frequency;
        const uint64_t remainder     = ticks % frequency;

        return (whole_seconds * NANOSECONDS_PER_SECOND) + ((remainder * NANOSECONDS_PER_SECOND) / frequency);
    }

    //  The inverse, rounded UP so a deadline computed from it is never early.  Exact for the same range.

    constexpr uint64_t NanosecondsToTicks(uint64_t nanoseconds, uint64_t frequency) noexcept
    {
        const uint64_t whole_seconds = nanoseconds / NANOSECONDS_PER_SECOND;
        const uint64_t remainder     = nanoseconds % NANOSECONDS_PER_SECOND;

        return (whole_seconds * frequency) + (((remainder * frequency) + NANOSECONDS_PER_SECOND - 1) / NANOSECONDS_PER_SECOND);
    }

    //  Microseconds to ticks, rounded UP like NanosecondsToTicks.  A span too long for 64-bit ticks saturates to
    //      UINT64_MAX rather than wrapping to a short one.

    constexpr uint64_t MicrosecondsToTicks(uint64_t microseconds, uint64_t frequency) noexcept
    {
        const uint64_t whole_seconds   = microseconds / MICROSECONDS_PER_SECOND;
        const uint64_t remainder       = microseconds % MICROSECONDS_PER_SECOND;
        const uint64_t remainder_ticks = ((remainder * frequency) + MICROSECONDS_PER_SECOND - 1) / MICROSECONDS_PER_SECOND;

        uint64_t whole_ticks = 0;
        uint64_t ticks       = 0;

        if (__builtin_mul_overflow(whole_seconds, frequency, &whole_ticks) ||
            __builtin_add_overflow(whole_ticks, remainder_ticks, &ticks))
        {
            return UINT64_MAX;
        }

        return ticks;
    }

    //  The counter value microseconds after now_ticks.  Saturates to UINT64_MAX, a deadline the counter never
    //      reaches, so an absurdly long wait waits forever instead of ending early.

    constexpr uint64_t DeadlineTicksAfter(uint64_t now_ticks, uint64_t microseconds, uint64_t frequency) noexcept
    {
        uint64_t deadline_ticks = 0;

        if (__builtin_add_overflow(now_ticks, MicrosecondsToTicks(microseconds, frequency), &deadline_ticks))
        {
            return UINT64_MAX;
        }

        return deadline_ticks;
    }
} // namespace utility
