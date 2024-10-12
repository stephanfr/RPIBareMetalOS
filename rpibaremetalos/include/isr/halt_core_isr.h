// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "isr/isr.h"

class HaltCoreISR : public InterruptServiceRoutine
{
public:
    constexpr Interrupts InterruptType() const noexcept override
    {
        return Interrupts::CORE_HALT;
    }

    constexpr InterruptServiceRoutineType ISRType() const noexcept override
    {
        return InterruptServiceRoutineType::HALT_CORE;
    }

    const char *Name() const noexcept override
    {
        return "Halt Core ISR";
    }

    void HandleInterrupt() override;
};

