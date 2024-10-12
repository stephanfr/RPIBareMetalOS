// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef enum class Interrupts : int32_t
{
    NO_SUCH_INTERRUPT = 0,

    CORE_HALT = 1,
    SYSTEM_TIMER_0 = 2,
    SYSTEM_TIMER_1 = 3,
    SYSTEM_TIMER_2 = 4,
    SYSTEM_TIMER_3 = 5,
} Interrupts;

const char *ToString(Interrupts interrupt);

typedef enum InterruptServiceRoutineType : uint32_t
{
    UNIDENTIFIED = 0,

    HALT_CORE = 1,
    SYSTEM_TIMER_RESCHEDULE = 2,
    TASK_SCHEDULER = 3
} InterruptServiceRoutineType;

class InterruptServiceRoutine
{
public:
    virtual constexpr Interrupts InterruptType() const noexcept = 0;

    virtual constexpr InterruptServiceRoutineType ISRType() const noexcept = 0;

    virtual const char *Name() const noexcept = 0;

    virtual void HandleInterrupt() = 0;
};
