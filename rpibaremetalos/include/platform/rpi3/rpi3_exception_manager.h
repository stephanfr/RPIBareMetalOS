// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once


#include "platform/platform_info.h"
#include "platform/exception_manager.h"

#include <minimalstdio.h>


typedef enum class BCM2837ARMCInterruptRequestRegisters
{
    REQUEST_BASIC_PENDING = 0x0000B200,
    REQUEST_PENDING_1 = 0x0000B204,
    REQUEST_PENDING_2 = 0x0000B208,
    FAST_INTERRUPT_REQUEST_PENDING = 0x0000B20C,
    ENABLE_IRQS_1 = 0x0000B210,
    ENABLE_IRQS_2 = 0x0000B214,
    ENABLE_BASIC_IRQS = 0x0000B218,
    DISABLE_IRQS_1 = 0x0000B21C,
    DISABLE_IRQS_2 = 0x0000B220,
    DISABLE_BASIC_IRQS = 0x0000B224
} BCM2837ARMCInterruptRequestRegisters;

//
//	System Timers 0 and 2 are reserved for the GPU on the 2837
//

typedef enum class BCM2837Interrupts : int32_t
{
    NO_SUCH_INTERRUPT = -1,

    SYSTEM_TIMER_1 = 2,
    SYSTEM_TIMER_3 = 8
} BCM2837Interrupts;



class BCM2837ExceptionManager : public ExceptionManager
{
public:
    BCM2837ExceptionManager()
        : platform_info(GetPlatformInfo())
    {
    }

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr) override
    {
        BCM2837Interrupts interrupt_type = GetBCM2837InterruptType(isr->InterruptType());

        if (interrupt_type == BCM2837Interrupts::NO_SUCH_INTERRUPT)
        {
            return false;
        }

        return ExceptionManager::AddISR(isr);
    }

    void HandleInterrupt() override
    {
        uint32_t irq = GetRegister(BCM2837ARMCInterruptRequestRegisters::REQUEST_PENDING_1);

        ISRPointerList *isrs = GetISRs(GetInterruptType(irq));

        if (isrs != nullptr)
        {
            for (InterruptServiceRoutine *current_isr : *isrs)
            {
                current_isr->HandleInterrupt();
            }
        }
        else
        {
            printf("No ISRs found for Interrupt: %u\n", irq);
        }
    }

private:
    const PlatformInfo &platform_info;

    uint32_t GetRegister(BCM2837ARMCInterruptRequestRegisters reg)
    {
        return *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg));
    }

    void SetRegister(BCM2837ARMCInterruptRequestRegisters reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg)) = value;
    }

    BCM2837Interrupts GetBCM2837InterruptType(Interrupts interrupt)
    {
        switch (interrupt)
        {
        case Interrupts::SYSTEM_TIMER_1:
            return BCM2837Interrupts::SYSTEM_TIMER_1;

        case Interrupts::SYSTEM_TIMER_3:
            return BCM2837Interrupts::SYSTEM_TIMER_3;

        default:
            return BCM2837Interrupts::NO_SUCH_INTERRUPT;
        }

        return BCM2837Interrupts::NO_SUCH_INTERRUPT;
    }

    Interrupts GetInterruptType(uint32_t interrupt)
    {
        switch (static_cast<BCM2837Interrupts>(interrupt))
        {
        case BCM2837Interrupts::SYSTEM_TIMER_1:
            return Interrupts::SYSTEM_TIMER_1;

        case BCM2837Interrupts::SYSTEM_TIMER_3:
            return Interrupts::SYSTEM_TIMER_3;

        default:
            return Interrupts::NO_SUCH_INTERRUPT;
        }

        return Interrupts::NO_SUCH_INTERRUPT;
    }

    bool EnableInterrupt(Interrupts interrupt_to_enable) override
    {
        switch (interrupt_to_enable)
        {
        case Interrupts::NO_SUCH_INTERRUPT:
        case Interrupts::SYSTEM_TIMER_0:
        case Interrupts::SYSTEM_TIMER_2:
            return false;

        case Interrupts::SYSTEM_TIMER_1:
            SetRegister(BCM2837ARMCInterruptRequestRegisters::ENABLE_IRQS_1, (uint32_t)BCM2837Interrupts::SYSTEM_TIMER_1);
            return true;

        case Interrupts::SYSTEM_TIMER_3:
            SetRegister(BCM2837ARMCInterruptRequestRegisters::ENABLE_IRQS_1, (uint32_t)BCM2837Interrupts::SYSTEM_TIMER_3);
            return true;
        }

        return false;
    }
};
