// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef enum class Interrupts : int32_t
{
    NO_SUCH_INTERRUPT = 0,

    CORE_HALT = 1,
    SWITCH_TASK = 2,
    SYSTEM_TIMER_0 = 64,
    SYSTEM_TIMER_1 = 65,
    SYSTEM_TIMER_2 = 66,
    SYSTEM_TIMER_3 = 67,
} Interrupts;

const char *ToString(Interrupts interrupt);

typedef enum InterruptServiceRoutineType : uint32_t
{
    UNIDENTIFIED = 0,

    HALT_CORE = 1,
    IMPERATIVE_CORE_TASK_SWITCH = 2,
    SYSTEM_TIMER_RESCHEDULE = 3,
    TASK_SCHEDULER = 4
} InterruptServiceRoutineType;

class InterruptServiceRoutine
{
public:
    virtual constexpr Interrupts InterruptType() const noexcept = 0;

    virtual constexpr InterruptServiceRoutineType ISRType() const noexcept = 0;

    virtual const char *Name() const noexcept = 0;

    virtual void HandleInterrupt() = 0;
};
