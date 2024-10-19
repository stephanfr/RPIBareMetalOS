// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

class PhysicalTimer
{
public:
    PhysicalTimer() = delete;
    PhysicalTimer(const PhysicalTimer &) = delete;
    PhysicalTimer(PhysicalTimer &&) = delete;

    PhysicalTimer &operator=(const PhysicalTimer &) = delete;
    PhysicalTimer &operator=(PhysicalTimer &&) = delete;

    static void WaitMsec(uint32_t msec_to_wait);
    
    static uint64_t CurrentTicks(void)
    {
        unsigned long long current_count;

        asm volatile("mrs %0, cntpct_el0" : "=r"(current_count));

        return current_count;
    }
};