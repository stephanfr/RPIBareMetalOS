// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "asm_utility.h"
#include "platform/exception_manager.h"
#include "platform/platform_info.h"

#include <devices/log.h>

//
//  For both the RPI3 and RPI4, the core mailboxes are used to send Inter processor interrupts.  This requires a bit more plumbing
//      on the RPI4 as the GIC-400 is used to distribute interrupts and the core mailboxes interrupts must be enabled on the GIC-400
//      and the interrupt forwarded to the correct core - in addition to enabling the core mailbox interrupts themselves which is 
//      all that is required on the RPI3.
//
//  I prefer using the core mailboxes for IPIs instead of SGIs on the RPI4 as the mechanism is the same on the RPI3 and 4 and it is
//      possible to send along more information as the mailbox is 32 bits wide.  The message deposited in the mailbox is the IPI number
//      but additional information could be included in the message, like the originating core or task.
//

typedef enum class BCM2711ARMCoreLocalPeripheralRegisterOffsets : uint32_t
{
    MAILBOX_INTERRUPT_CONTROL_OFFSET = 0x50,
    INTERRUPT_SOURCE_OFFSET = 0x60,
    MAILBOX_WRITE_OFFSET = 0x80,
    MAILBOX_READ_CLEAR_OFFSET = 0xC0
} BCM2711ARMCoreLocalPeripheralRegisterOffsets;

typedef enum class BCM2711GenericInterruptControllerRegisters : uint32_t
{
    ENABLE_IRQ_BASE = 0x00001100,
    DISABLE_IRQ_BASE = 0x00001180,
    INTERRUPT_CPU_TARGET_BASE = 0x00001800,
    INTERRUPT_ACKNOWLEDGE = 0x0000200C,
    END_OF_INTERRUPT = 0x00002010
} BCM2711GenericInterruptControllerRegisters;

//  Interrupts are mapped from the Video Core for the RPI4

typedef enum class BCM2711Interrupts : int32_t
{
    NO_SUCH_INTERRUPT = -1,

    HALT_CORE = static_cast<uint32_t>(InterprocessorInterrupts::HALT),
    CORE_MAILBOX_0 = 0x20, //  0x20 is the base for all core mailboxes.  The actual mailbox is 0x20 + (4 * core_id) + mailbox_id
    CORE_MAILBOX_1 = 0x21,
    CORE_MAILBOX_2 = 0x22,
    CORE_MAILBOX_3 = 0x23,
    SYSTEM_TIMER_0 = 0x60,
    SYSTEM_TIMER_1 = 0x61,
    SYSTEM_TIMER_2 = 0x62,
    SYSTEM_TIMER_3 = 0x63
} BCM2711Interrupts;

inline bool IsMailboxInterrupt(BCM2711Interrupts interrupt)
{
    return (interrupt >= BCM2711Interrupts::CORE_MAILBOX_0 && interrupt <= BCM2711Interrupts::CORE_MAILBOX_3);
}

class BCM2711ExceptionManager : public ExceptionManager
{
public:
    BCM2711ExceptionManager()
        : platform_info(GetPlatformInfo())
    {
    }

    bool Initialize() override;

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr, CoreList on_cores) override
    {
        return ExceptionManager::AddISR(isr, on_cores);
    }

    bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) override;

    void HandleInterrupt() override;

private:
    constexpr static uint32_t ARM_GICD_BASE = 0xFF841000;

    //  GIC400 Software Generated Interrupt Registers

    constexpr static uint32_t GICD_SGIR = ARM_GICD_BASE + 0x0F00;
    constexpr static uint32_t GICD_SGIR_CPU_TARGET_LIST__SHIFT = 16;

    const PlatformInfo &platform_info;

    const uint8_t *BCM2711_GIC400_BASE = reinterpret_cast<const uint8_t *>(0xFF840000);

    bool EnableInterrupt(Interrupts interrupt_to_enable, CoreList on_cores) override;
    bool DisableInterrupt(Interrupts interrupt_to_disable, CoreList on_cores) override;

    uint32_t GetGICRegister(BCM2711GenericInterruptControllerRegisters reg)
    {
        return *((volatile uint32_t *)(BCM2711_GIC400_BASE + (uint32_t)reg));
    }

    void SetGICRegister(BCM2711GenericInterruptControllerRegisters reg,
                        uint32_t value)
    {
        *((volatile uint32_t *)(BCM2711_GIC400_BASE + (uint32_t)reg)) = value;
    }

    void Enable2711Interrupt(uint32_t core_id, BCM2711Interrupts interrupt)
    {
        //  There are 256 interrupts which are enabled/disabled by bits in one of 8 32 bit registers

        uint32_t interrupt_num = static_cast<uint32_t>(interrupt) + (4 * core_id);

        unsigned int reg_num = interrupt_num / 32;
        unsigned int bit_mask = 1 << (interrupt_num % 32);

        unsigned int enableRegister = static_cast<uint32_t>(BCM2711GenericInterruptControllerRegisters::ENABLE_IRQ_BASE) + (4 * reg_num);

        uint32_t current_value = *((volatile uint32_t *)(BCM2711_GIC400_BASE + enableRegister));

        if ((current_value & bit_mask) == 0)
        {
            *((volatile uint32_t *)(BCM2711_GIC400_BASE + enableRegister)) = bit_mask;
            Assign2711InterruptCoreTarget(core_id, interrupt);
        }
    }

    void Disable2711Interrupt(uint32_t core_id, BCM2711Interrupts interrupt)
    {
        //  There are 256 interrupts which are enabled/disabled by bits in one of 8 32 bit registers

        uint32_t interrupt_num = static_cast<uint32_t>(interrupt) + (4 * core_id);

        unsigned int reg_num = interrupt_num / 32;
        unsigned int bit_mask = 1 << (interrupt_num % 32);

        unsigned int enableRegister = static_cast<uint32_t>(BCM2711GenericInterruptControllerRegisters::DISABLE_IRQ_BASE) + (4 * reg_num);

        uint32_t current_value = *((volatile uint32_t *)(BCM2711_GIC400_BASE + enableRegister));

        if ((current_value & bit_mask) == 0)
        {
            *((volatile uint32_t *)(BCM2711_GIC400_BASE + enableRegister)) = bit_mask;
            Assign2711InterruptCoreTarget(core_id, interrupt);
        }
    }

    void Assign2711InterruptCoreTarget(uint32_t core_id, BCM2711Interrupts interrupt)
    {
        unsigned int reg_num = static_cast<uint32_t>(interrupt) / 4;

        //  Special case for mailbox interrupts - the interrupt passed in is the base mailbox interrupt, so we need to adjust the register number

        if (IsMailboxInterrupt(interrupt))
        {
            reg_num += core_id;
        }

        unsigned int byte_offset = static_cast<uint32_t>(interrupt) % 4;
        unsigned int targetRegister = static_cast<uint32_t>(BCM2711GenericInterruptControllerRegisters::INTERRUPT_CPU_TARGET_BASE) + (4 * reg_num);

        *((volatile uint32_t *)(BCM2711_GIC400_BASE + targetRegister)) = (1 << ((byte_offset * 8) + core_id));
    }

    BCM2711Interrupts GetBCM2711InterruptType(Interrupts interrupt)
    {
        switch (interrupt)
        {
        case Interrupts::CORE_HALT:
            return BCM2711Interrupts::CORE_MAILBOX_3;

        case Interrupts::SWITCH_TASK:
            return BCM2711Interrupts::CORE_MAILBOX_3;

            //  Normally I'd use just one case - but gcc appears to not like the extra colons in the case statements

        case Interrupts::CORE_MAILBOX_0:
            return BCM2711Interrupts::CORE_MAILBOX_0;

        case Interrupts::CORE_MAILBOX_1:
            return BCM2711Interrupts::CORE_MAILBOX_1;

        case Interrupts::CORE_MAILBOX_2:
            return BCM2711Interrupts::CORE_MAILBOX_2;

        case Interrupts::CORE_MAILBOX_3:
            return BCM2711Interrupts::CORE_MAILBOX_3;

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
        //  First, hande the core mailboxes as we do not actually enumerate all of them

        if (interrupt >= 0x20 && interrupt <= 0x2F)
        {
            switch (interrupt & 0x03)
            {
            case 0:
                return Interrupts::CORE_MAILBOX_0;

            case 1:
                return Interrupts::CORE_MAILBOX_1;

            case 2:
                return Interrupts::CORE_MAILBOX_2;

            case 3:
                uint32_t mailbox_value = ReadCoreMailbox(GetCoreID(), 3); //  Mailbox 3 is used exclusively for IPIs.  The value is the IPI type
                ResetCoreMailbox(GetCoreID(), 3, mailbox_value);
                return ExceptionManager::AsInterrupt(static_cast<InterprocessorInterrupts>(mailbox_value));
            }
        }

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
            break;
        }

        return ExceptionManager::AsInterrupt(static_cast<InterprocessorInterrupts>(interrupt));
    }

    void EnableCoreMailbox(uint32_t core_id, uint32_t mailbox_id)
    {
        *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2711ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + (4 * core_id)) = 0x00000001 << mailbox_id;

        //  Compute the actual interrupt number from the base mailbox, core_id and mailbox_id

        //        EnableInterrupt((Interrupts)(uint32_t(Interrupts::CORE_MAILBOX_0) + mailbox_id), CoreList(core_id));
    }

    void DisableCoreMailbox(uint32_t core_id, uint32_t mailbox_id)
    {
        *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2711ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + (4 * core_id)) &= ~(0x00000001 << mailbox_id);

        //  Compute the actual interrupt number from the base mailbox, core_id and mailbox_id

        //        EnableInterrupt((Interrupts)(uint32_t(Interrupts::CORE_MAILBOX_0) + mailbox_id), CoreList(core_id));
    }

    void SetCoreMailbox(uint32_t core_id, uint32_t mailbox_id, uint32_t value)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2711ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_WRITE_OFFSET + (core_id * 16) + (mailbox_id * 4));

        *reg = value;
    }

    uint32_t ReadCoreMailbox(uint32_t core_id, uint32_t mailbox_id)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2711ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_READ_CLEAR_OFFSET + (core_id * 16) + (mailbox_id * 4));

        return *reg;
    }

    void ResetCoreMailbox(uint32_t core_id, uint32_t mailbox_id, uint32_t value)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2711ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_READ_CLEAR_OFFSET + (core_id * 16) + (mailbox_id * 4));

        *reg = value;
    }
};
