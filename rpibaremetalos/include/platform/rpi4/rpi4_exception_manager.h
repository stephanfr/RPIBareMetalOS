// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/exception_manager.h"
#include "platform/platform_info.h"

#include <devices/log.h>

//
//  For the RPI4, the interrupt controller is the GIC-400 and we will use Software Generated Interrupts (SGI) to
//      send inter-processor interrupts.  I tried to use core mailboxes but could not find a way to get them to work
//      nor could I find any documentation showing how to use them.  Other examples used SGIs which should work
//      pretty much the same for the RPI5 and successive versions using the GIC400 or backward compatibles.
//
//  For the RPI3, there is no Generic Interrupt Controller, so we use the ARM Core Local Interrupt Controller and
//      the core mailboxes to send inter-processor interrupts.
//

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

    HALT_CORE = static_cast<uint32_t>(InterprocessorInterrupts::HALT),
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

    bool Initialize() override;

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr) override
    {
        return ExceptionManager::AddISR(isr);
    }

    bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) override;

    void HandleInterrupt() override;

private:
    constexpr static uint32_t ARM_GICD_BASE = 0xFF841000;
    constexpr static uint32_t GICD_SGIR = ARM_GICD_BASE + 0xF00;
    constexpr static uint32_t GICD_SGIR_CPU_TARGET_LIST__SHIFT = 16;

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
