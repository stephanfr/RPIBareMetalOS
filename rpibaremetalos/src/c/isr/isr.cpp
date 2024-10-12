// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "isr/isr.h"

const char *ToString(Interrupts interrupt)
{
    switch( interrupt )
    {
        case Interrupts::CORE_HALT:
            return "CORE_HALT";
        case Interrupts::SYSTEM_TIMER_0:
            return "SYSTEM_TIMER_0";
        case Interrupts::SYSTEM_TIMER_1:
            return "SYSTEM_TIMER_1";
        case Interrupts::SYSTEM_TIMER_2:
            return "SYSTEM_TIMER_2";
        case Interrupts::SYSTEM_TIMER_3:
            return "SYSTEM_TIMER_3";
        default:
            return "NO_SUCH_INTERRUPT";
    }
}

