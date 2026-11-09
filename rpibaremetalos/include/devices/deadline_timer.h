// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "asm_config.h"
#include "asm_utility.h"

//  Each core's ARM virtual timer (CNTV), used to interrupt at that core's earliest wait deadline.  CNTVOFF_EL2 is
//      zeroed at boot, so the virtual count equals the physical count every deadline is computed from.
//
//  Owning core only, with IRQs disabled.

class DeadlineTimer
{
private:

    static constexpr uint64_t CNTV_CTL_ENABLE = 0x1;
    static constexpr uint64_t CNTV_CTL_IMASK = 0x2;

public:

    DeadlineTimer() = delete;

    //  Interrupts once the system counter reaches deadline_ticks, at once if it already has; 0 disarms.

    static void ArmAt(uint64_t deadline_ticks)
    {
        const uint32_t core = GetCoreID();

        if (armed_ticks_[core] == deadline_ticks)
        {
            return;
        }

        armed_ticks_[core] = deadline_ticks;

        if (deadline_ticks == 0)
        {
            SetControl(CNTV_CTL_ENABLE | CNTV_CTL_IMASK);
            return;
        }

        asm volatile("msr cntv_cval_el0, %0" ::"r"(deadline_ticks));
        SetControl(CNTV_CTL_ENABLE);
    }

    //  The timer's interrupt is level-sensitive: the ISR must disarm before EOI or it fires again at once.

    static void Disarm()
    {
        armed_ticks_[GetCoreID()] = 0;
        SetControl(CNTV_CTL_ENABLE | CNTV_CTL_IMASK);
    }

private:

    static inline uint64_t armed_ticks_[MAX_CORES] = {};

    static void SetControl(uint64_t control)
    {
        asm volatile("msr cntv_ctl_el0, %0" ::"r"(control));
        INSTRUCTION_CACHE_BARRIER;
    }
};
