// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/exception_manager.h"

//  Minimal stub -- RPi5's GIC-400 sits at a different base address than
//      RPi4's (0x10_7FFF9000 vs 0xFF841000) and ConfigureGIC400 has not yet
//      been parameterized to support it (deferred, see the RPi5 addendum).
//      This class exists ONLY so InitializePlatform()'s unconditional
//      GetExceptionManager().Initialize() call has a non-null target --
//      without it, RPi5 dereferences a null __exception_manager immediately
//      after the framebuffer console is allocated, crashing before any text
//      is ever printed. Every method here is a safe no-op; no interrupt
//      source is actually enabled for RPi5 yet.

class RPI5ExceptionManager : public ExceptionManager
{
public:
    RPI5ExceptionManager() = default;

    bool Initialize() override
    {
        return true;
    }

    bool EnableInterrupt(Interrupts interrupt_to_enable, CoreList on_cores) override
    {
        (void)interrupt_to_enable;
        (void)on_cores;
        return false;
    }

    bool DisableInterrupt(Interrupts interrupt_to_disable, CoreList on_cores) override
    {
        (void)interrupt_to_disable;
        (void)on_cores;
        return false;
    }

    bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) override
    {
        (void)core_id;
        (void)ipi_id;
        return false;
    }

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr, CoreList on_cores) override
    {
        (void)isr;
        (void)on_cores;
        return false;
    }

    void HandleInterrupt() override
    {
    }
};