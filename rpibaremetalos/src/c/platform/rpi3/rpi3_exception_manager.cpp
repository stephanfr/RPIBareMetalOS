// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi3/rpi3_exception_manager.h"

#include "asm_utility.h"

bool BCM2837ExceptionManager::Initialize()
{
    //  TODO - check initialization sequence

    LogEntryAndExit("Entering Initialize\n");

    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET) = 0x00000001 << IPI_MAILBOX_ID;
    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + 4) = 0x00000001 << IPI_MAILBOX_ID;
    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + 8) = 0x00000001 << IPI_MAILBOX_ID;
    *reinterpret_cast<uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::MAILBOX_INTERRUPT_CONTROL_OFFSET + 12) = 0x00000001 << IPI_MAILBOX_ID;

    //  GPU IRQs and FIQs to core 0.  This is the reset value; set it anyway rather than trust the firmware.

    *reinterpret_cast<volatile uint32_t *>(platform_info.GetARMLocalBase() + (uint32_t)BCM2837ARMCoreLocalPeripheralRegisterOffsets::GPU_INTERRUPT_ROUTING_OFFSET) = 0;

    return true;
}

bool BCM2837ExceptionManager::SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id)
{
    LogEntryAndExit("Entering SendInterprocessorInterrupt: Core: %d, IPI Id: %d\n", core_id, static_cast<uint32_t>(ipi_id));

    if (core_id > GetPlatformInfo().GetNumberOfCores())
    {
        return false;
    }

    SetCoreMailbox(core_id, IPI_MAILBOX_ID, static_cast<uint32_t>(ipi_id));

    return true;
}

void BCM2837ExceptionManager::HandleInterrupt()
{
    LogEntryAndExit("Entering HandleInterrupt\n");

    uint32_t core_id = GetCoreID();
    uint32_t interrupt_source = GetCoreLocalInterruptSource(core_id);

    InterruptServiceRoutine *core_task_switch_isr = nullptr;

    if ((interrupt_source & static_cast<BCM2837ARMLocalInterruptSources>((uint32_t)BCM2837ARMLocalInterruptSources::MAILBOX_0 << IPI_MAILBOX_ID)) != BCM2837ARMLocalInterruptSources::NONE)
    {
        uint32_t ipi_payload = GetCoreMailbox(core_id, IPI_MAILBOX_ID);

        //  Clear by writing back exactly the bits that were read, which is
        //      what the mailbox's write-high-to-clear register expects.

        ResetCoreMailbox(core_id, IPI_MAILBOX_ID, ipi_payload);

        bool recognized_any = DispatchIPIMailboxPayload(ipi_payload, [&](Interrupts interrupt)
        {
            DispatchInterruptType(interrupt, core_task_switch_isr);
        });

        if (!recognized_any)
        {
            LogWarning("Unhandled IPI mailbox payload: %u\n", ipi_payload);
        }
    }
    else if ((interrupt_source & BCM2837ARMLocalInterruptSources::CNTVIRQ) != BCM2837ARMLocalInterruptSources::NONE)
    {
        DispatchInterruptType(Interrupts::ARM_VIRTUAL_TIMER, core_task_switch_isr);
    }
    else if ((interrupt_source & BCM2837ARMLocalInterruptSources::GPU_INTERRUPT) != BCM2837ARMLocalInterruptSources::NONE)
    {
        //  Several GPU interrupts may be pending at once: dispatch each.

        const uint32_t pending_1 = GetRegister(BCM2837ARMCInterruptRequestRegisters::REQUEST_PENDING_1);
        const uint32_t pending_2 = GetRegister(BCM2837ARMCInterruptRequestRegisters::REQUEST_PENDING_2);

        bool dispatched_any = false;

        auto dispatch_if_pending = [&](uint32_t pending, BCM2837Interrupts bit, Interrupts interrupt)
        {
            if ((pending & static_cast<uint32_t>(bit)) == 0)
            {
                return;
            }

            if (!DispatchInterruptType(interrupt, core_task_switch_isr))
            {
                LogError("No ISRs found for Interrupt: %s\n", ToString(interrupt));
            }

            dispatched_any = true;
        };

        dispatch_if_pending(pending_1, BCM2837Interrupts::SYSTEM_TIMER_1, Interrupts::SYSTEM_TIMER_1);
        dispatch_if_pending(pending_1, BCM2837Interrupts::SYSTEM_TIMER_3, Interrupts::SYSTEM_TIMER_3);
        dispatch_if_pending(pending_2, BCM2837Interrupts::UART0, Interrupts::UART0);

        if (!dispatched_any)
        {
            LogWarning("Unhandled GPU interrupt: pending 1 %08x, pending 2 %08x\n", pending_1, pending_2);
        }
    }

    ExitInterrupt(core_task_switch_isr);
}
