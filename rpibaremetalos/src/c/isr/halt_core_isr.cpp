// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "isr/halt_core_isr.h"

#include "asm_utility.h"

#include <devices/log.h>

void HaltCoreISR::HandleInterrupt()
{
    LogEntryAndExit("HaltCoreISR::HandleInterrupt\n");

    //  Halt the core

    ParkCore();
}
