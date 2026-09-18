// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "isr.h"

#include "devices/scheduler_clock.h"

class TaskSwitchISR : public InterruptServiceRoutine
{
public:

    Interrupts InterruptType() const noexcept override
    {
        return GetSchedulerClock().TickInterrupt();
    }

    constexpr InterruptServiceRoutineType ISRType() const noexcept override
    {
        return InterruptServiceRoutineType::TASK_SCHEDULER;
    }

    const char *Name() const noexcept override
    {
        return "Task Switch ISR";
    }

    void HandleInterrupt() override;
};
