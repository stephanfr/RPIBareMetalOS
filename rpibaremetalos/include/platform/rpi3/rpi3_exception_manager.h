// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/exception_manager.h"
#include "platform/platform_info.h"

#include "devices/log.h"

//  Enumerate the interrupt sources for the ARM Core.
//      These are described in the BCM2836 Peripherals manual QA7, section 4.10
//      Define an and operator to allow bitwise operations on the enum values.
//
//  For interprocessor interrupts, we use the core mailboxes.  At present mailbox 3
//      is used to send the IPI number to the correct core.  On RPI4 and later SOCs,
//      we use the GIC-400 SGI registers to send IPIs.

typedef enum class BCM2837ARMLocalInterruptSources : uint32_t
{
    NONE = 0x0000,
    CNTPSIRQ = 0x0001,
    CNTPNSIRQ = 0x0002,
    CNTHPIRQ = 0x0004,
    CNTVIRQ = 0x0008,
    MAILBOX_0 = 0x0010,
    MAILBOX_1 = 0x0020,
    MAILBOX_2 = 0x0040,
    MAILBOX_3 = 0x0080,
    ANY_MAILBOX = 0x00F0, //  OR of all mailbox interrupts
    GPU_INTERRUPT = 0x0100,
    PMU_INTERRUPT = 0x0200,
    AXI_OUTSTANDING = 0x0400,
    LOCAL_TIMER = 0x0800
} BCM2837ARMLocalInterruptSources;

inline BCM2837ARMLocalInterruptSources operator&(uint32_t lhs, BCM2837ARMLocalInterruptSources rhs)
{
    return static_cast<BCM2837ARMLocalInterruptSources>(lhs & static_cast<uint32_t>(rhs));
}

typedef enum class BCM2837ARMCoreLocalPeripheralRegisterOffsets : uint32_t
{
    MAILBOX_INTERRUPT_CONTROL_OFFSET = 0x50,
    INTERRUPT_SOURCE_OFFSET = 0x60,
    MAILBOX_WRITE_OFFSET = 0x80,
    MAILBOX_READ_CLEAR_OFFSET = 0xC0
} BCM2837ARMCoreLocalPeripheralRegisterOffsets;

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

    bool Initialize() override;

    bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) override;

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr, CoreList on_cores) override
    {
        return ExceptionManager::AddISR(isr, on_cores);
    }

    void HandleInterrupt() override;

private:
    static constexpr uint32_t IPI_MAILBOX_ID = 3;

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

    uint32_t GetCoreLocalInterruptSource(uint32_t core)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::INTERRUPT_SOURCE_OFFSET + (core * 4));

        return *reg;
    }

    void SetCoreMailbox(uint32_t core, uint32_t mailbox_id, uint32_t value)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_WRITE_OFFSET + (core * 16) + (mailbox_id * 4));

        *reg = value;
    }

    void ResetCoreMailbox(uint32_t core, uint32_t mailbox_id, uint32_t value)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_READ_CLEAR_OFFSET + (core * 16) + (mailbox_id * 4));

        *reg = value;
    }

    uint32_t GetCoreMailbox(uint32_t core, uint32_t mailbox_id)
    {
        volatile uint32_t *reg = reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_READ_CLEAR_OFFSET + (core * 16) + (mailbox_id * 4));

        return *reg;
    }

    int32_t GetMailboxId(uint32_t interrupt_source)
    {
        switch (interrupt_source)
        {
        case static_cast<uint32_t>(BCM2837ARMLocalInterruptSources::MAILBOX_0):
            return 0;
        case static_cast<uint32_t>(BCM2837ARMLocalInterruptSources::MAILBOX_1):
            return 1;
        case static_cast<uint32_t>(BCM2837ARMLocalInterruptSources::MAILBOX_2):
            return 2;
        case static_cast<uint32_t>(BCM2837ARMLocalInterruptSources::MAILBOX_3):
            return 3;
        default:
            return -1;
        }
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

    Interrupts AsInterrupt(BCM2837Interrupts interrupt)
    {
        switch (interrupt)
        {
        case BCM2837Interrupts::SYSTEM_TIMER_1:
            return Interrupts::SYSTEM_TIMER_1;

        case BCM2837Interrupts::SYSTEM_TIMER_3:
            return Interrupts::SYSTEM_TIMER_3;

        default:
            LogError("No such interrupt: %d\n", interrupt);

            return Interrupts::NO_SUCH_INTERRUPT;
        }

        return Interrupts::NO_SUCH_INTERRUPT;
    }

    bool EnableInterrupt(Interrupts interrupt_to_enable, CoreList on_cores) override
    {
        switch (interrupt_to_enable)
        {
        case Interrupts::NO_SUCH_INTERRUPT:
        case Interrupts::SYSTEM_TIMER_0:
        case Interrupts::SYSTEM_TIMER_2:
            return false;

        case Interrupts::CORE_HALT: //  Already enabled in 'Initialize'
        case Interrupts::SWITCH_TASK:
            return true;

        case Interrupts::CORE_MAILBOX_0:
            for (uint32_t current_core = 0; current_core < platform_info.GetNumberOfCores(); current_core++)
            {
                if (on_cores.Cores() & (1 << current_core))
                {
                    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + (4 * current_core)) = 0x00000001;
                }
            }
            return true;

        case Interrupts::CORE_MAILBOX_1:
            for (uint32_t current_core = 0; current_core < platform_info.GetNumberOfCores(); current_core++)
            {
                if (on_cores.Cores() & (1 << current_core))
                {
                    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + (4 * current_core)) = 0x00000001 << 1;
                }
            }
            return true;

        case Interrupts::CORE_MAILBOX_2:
            for (uint32_t current_core = 0; current_core < platform_info.GetNumberOfCores(); current_core++)
            {
                if (on_cores.Cores() & (1 << current_core))
                {
                    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + (4 * current_core)) = 0x00000001 << 2;
                }
            }
            return true;

        case Interrupts::CORE_MAILBOX_3:
            for (uint32_t current_core = 0; current_core < platform_info.GetNumberOfCores(); current_core++)
            {
                if (on_cores.Cores() & (1 << current_core))
                {
                    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + (4 * current_core)) = 0x00000001 << 3;
                }
            }
            return true;

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
