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
} // namespace utility
