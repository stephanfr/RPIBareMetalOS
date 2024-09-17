// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "isr.h"

class SystemTimerRescheduleISR : public InterruptServiceRoutine
{
public:
    constexpr Interrupts InterruptType() const noexcept override
    {
        return Interrupts::SYSTEM_TIMER_1;
    }

    constexpr InterruptServiceRoutineType ISRType() const noexcept override
    {
        return InterruptServiceRoutineType::SYSTEM_TIMER_RESCHEDULE;
    }

    const char* Name() const noexcept override
    {
        return "Timer Reschedule ISR";
    }

    void HandleInterrupt() override;
};
