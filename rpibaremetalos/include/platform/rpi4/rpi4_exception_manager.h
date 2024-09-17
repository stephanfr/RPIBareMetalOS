// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/exception_manager.h"
#include "platform/platform_info.h"

#include <devices/log.h>

typedef enum class BCM2711GenericInterruptControllerRegisters : uint32_t
{
    ENABLE_IRQ_BASE = 0x00001100,
    INTERRUPT_CPU_TARGET_BASE = 0x00001800,
    INTERRUPT_ACKNOWLEDGE = 0x0000200C,
    END_OF_INTERRUPT = 0x00002010
} BCM2711GenericInterruptControllerRegisters;

//  Interrupts are mapped from the Video Core for the RPI4

typedef enum class BCM2711Interrupts : int32_t
{
    NO_SUCH_INTERRUPT = -1,

    SYSTEM_TIMER_0 = 0x60,
    SYSTEM_TIMER_1 = 0x61,
    SYSTEM_TIMER_2 = 0x62,
    SYSTEM_TIMER_3 = 0x63
} BCM2711Interrupts;

class BCM2711ExceptionManager : public ExceptionManager
{
public:
    BCM2711ExceptionManager()
        : platform_info(GetPlatformInfo())
    {
    }

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr) override
    {
        BCM2711Interrupts interrupt_type = GetBCM2711InterruptType(isr->InterruptType());

        if (interrupt_type == BCM2711Interrupts::NO_SUCH_INTERRUPT)
        {
            return false;
        }

        return ExceptionManager::AddISR(isr);
    }

    void HandleInterrupt() override
    {
        LogEntryAndExit("Entering HandleInterrupt");

        unsigned int irq_ack_reg = GetGICRegister(BCM2711GenericInterruptControllerRegisters::INTERRUPT_ACKNOWLEDGE);
        unsigned int irq = irq_ack_reg & 0x2FF;

        ISRPointerList *isrs = GetISRs(GetInterruptType(irq));

        //  The task switch ISR is special as it may never return.  Therefore, trap it and we execute it last.

        InterruptServiceRoutine *task_switch_isr = nullptr;

        if (isrs != nullptr)
        {
            for (InterruptServiceRoutine *current_isr : *isrs)
            {
                if( current_isr->ISRType() != InterruptServiceRoutineType::TASK_SCHEDULER )
                {
                    current_isr->HandleInterrupt();
                }
                else
                {
                    task_switch_isr = current_isr;
                }
            }
        }
        else
        {
            LogInfo("No ISRs found for Interrupt: %u\n", irq);
        }

        //  Let the GIC know we have serviced the interrupt.  End of interrupt ordering MUST mirror the acknowledge ordering,
        //      this needs to be enforced even with nested interrupts.

        SetGICRegister(BCM2711GenericInterruptControllerRegisters::END_OF_INTERRUPT, irq_ack_reg);

        //  Interrupt has been acknowledged and all other ISRs handled, execute the task scheduler now if we have one.

        if( task_switch_isr != nullptr )
        {
            task_switch_isr->HandleInterrupt();
        }
    }

private:
    const PlatformInfo &platform_info;

    const uint8_t *BCM2711_GIC400_BASE = reinterpret_cast<const uint8_t *>(0xFF840000);

    uint32_t GetGICRegister(BCM2711GenericInterruptControllerRegisters reg)
    {
        return *((volatile uint32_t *)(BCM2711_GIC400_BASE + (uint32_t)reg));
    }

    void SetGICRegister(BCM2711GenericInterruptControllerRegisters reg,
                        uint32_t value)
    {
        *((volatile uint32_t *)(BCM2711_GIC400_BASE + (uint32_t)reg)) = value;
    }

    void Enable2711Interrupt(BCM2711Interrupts interrupt)
    {
        //  There are 256 interrupts which are enabled/disabled by bits in one of 8 32 bit registers

        unsigned int reg_num = static_cast<uint32_t>(interrupt) / 32;
        unsigned int interrupt_bit_offset = static_cast<uint32_t>(interrupt) % 32;

        unsigned int enableRegister = static_cast<uint32_t>(BCM2711GenericInterruptControllerRegisters::ENABLE_IRQ_BASE) + (4 * reg_num);

        *((volatile uint32_t *)(BCM2711_GIC400_BASE + enableRegister)) = (1 << interrupt_bit_offset);
    }

    void Assign2711InterruptCPU0Target(BCM2711Interrupts interrupt)
    {
        unsigned int reg_num = static_cast<uint32_t>(interrupt) / 4;
        unsigned int targetRegister = static_cast<uint32_t>(BCM2711GenericInterruptControllerRegisters::INTERRUPT_CPU_TARGET_BASE) + (4 * reg_num);

        *((volatile uint32_t *)(BCM2711_GIC400_BASE + targetRegister)) = (1 << 8);
    }

    BCM2711Interrupts GetBCM2711InterruptType(Interrupts interrupt)
    {
        switch (interrupt)
        {
        case Interrupts::SYSTEM_TIMER_0:
            return BCM2711Interrupts::SYSTEM_TIMER_0;

        case Interrupts::SYSTEM_TIMER_1:
            return BCM2711Interrupts::SYSTEM_TIMER_1;

        case Interrupts::SYSTEM_TIMER_2:
            return BCM2711Interrupts::SYSTEM_TIMER_2;

        case Interrupts::SYSTEM_TIMER_3:
            return BCM2711Interrupts::SYSTEM_TIMER_3;

        default:
            return BCM2711Interrupts::NO_SUCH_INTERRUPT;
        }

        return BCM2711Interrupts::NO_SUCH_INTERRUPT;
    }

    Interrupts GetInterruptType(uint32_t interrupt)
    {
        switch (static_cast<BCM2711Interrupts>(interrupt))
        {

        case BCM2711Interrupts::SYSTEM_TIMER_0:
            return Interrupts::SYSTEM_TIMER_0;

        case BCM2711Interrupts::SYSTEM_TIMER_1:
            return Interrupts::SYSTEM_TIMER_1;

        case BCM2711Interrupts::SYSTEM_TIMER_2:
            return Interrupts::SYSTEM_TIMER_2;

        case BCM2711Interrupts::SYSTEM_TIMER_3:
            return Interrupts::SYSTEM_TIMER_3;

        default:
            return Interrupts::NO_SUCH_INTERRUPT;
        }

        return Interrupts::NO_SUCH_INTERRUPT;
    }

    bool EnableInterrupt(Interrupts interrupt_to_enable) override
    {
        BCM2711Interrupts bcm_2711_interrupt = GetBCM2711InterruptType(interrupt_to_enable);

        if (bcm_2711_interrupt != BCM2711Interrupts::NO_SUCH_INTERRUPT)
        {
            Assign2711InterruptCPU0Target(bcm_2711_interrupt);
            Enable2711Interrupt(bcm_2711_interrupt);

            return true;
        }

        return false;
    }
};
