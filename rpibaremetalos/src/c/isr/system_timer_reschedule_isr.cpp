// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "isr/system_timer_reschedule_isr.h"

#include "devices/system_timer.h"

void SystemTimerRescheduleISR::HandleInterrupt()
{
    GetSystemTimer().RescheduleRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1);
}
