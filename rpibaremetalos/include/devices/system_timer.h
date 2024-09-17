// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/platform_info.h"

typedef enum class SystemTimerCompares : uint32_t
{
    TIMER_COMPARE_0 = 0x0000300C,
    TIMER_COMPARE_1 = 0x00003010,
    TIMER_COMPARE_2 = 0x00003014,
    TIMER_COMPARE_3 = 0x00003018,
} SystemTimerCompares;

class SystemTimer
{
public:
    virtual uint64_t GetMicroseconds() const = 0;

    virtual void WaitInMicroseconds(uint32_t microseconds_to_wait) const = 0;

    virtual void StartRecurringInterrupt(SystemTimerCompares compare_register, uint32_t period_in_microseconds) = 0;

    virtual void CancelRecurringInterrupt(SystemTimerCompares compare_register) = 0;

    virtual void RescheduleRecurringInterrupt(SystemTimerCompares compare_register, uint32_t new_period_in_microseconds = 0) = 0;
};

SystemTimer &GetSystemTimer();
