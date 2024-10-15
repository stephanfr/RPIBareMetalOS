// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "isr/isr.h"

class CoreTaskSwitchISR : public InterruptServiceRoutine
{
public:
    constexpr Interrupts InterruptType() const noexcept override
    {
        return Interrupts::SWITCH_TASK;
    }

    constexpr InterruptServiceRoutineType ISRType() const noexcept override
    {
        return InterruptServiceRoutineType::IMPERATIVE_CORE_TASK_SWITCH;
    }

    const char *Name() const noexcept override
    {
        return "Core Task Switch ISR";
    }

    void HandleInterrupt() override;
};

