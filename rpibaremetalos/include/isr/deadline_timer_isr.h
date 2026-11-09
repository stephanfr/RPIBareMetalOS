// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "isr/isr.h"

//  Runs on the core whose deadline passed.  The switch at IRQ exit expires the due timers and re-arms.

class DeadlineTimerISR : public InterruptServiceRoutine
{
public:

    constexpr Interrupts InterruptType() const noexcept override
    {
        return Interrupts::ARM_VIRTUAL_TIMER;
    }

    constexpr InterruptServiceRoutineType ISRType() const noexcept override
    {
        return InterruptServiceRoutineType::DEADLINE_TIMER;
    }

    const char *Name() const noexcept override
    {
        return "Deadline Timer ISR";
    }

    void HandleInterrupt() override;
};
