// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef enum class Interrupts : int32_t
{
    NO_SUCH_INTERRUPT = -1,

    SYSTEM_TIMER_0 = 0,
    SYSTEM_TIMER_1 = 1,
    SYSTEM_TIMER_2 = 2,
    SYSTEM_TIMER_3 = 3
} Interrupts;

class InterruptServiceRoutine
{
public:
    virtual constexpr Interrupts InterruptType() const noexcept = 0;

    virtual const char *Name() const noexcept = 0;

    virtual void HandleInterrupt() = 0;
};
