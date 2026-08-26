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

        Interrupts interrupt = DecodeIPIMailboxPayload(core_id, ipi_payload);

        bool recognized_any = DispatchIPIMailboxPayload(ipi_payload, [&](Interrupts interrupt)
        {
            DispatchInterruptType(interrupt, core_task_switch_isr);
        });

        if (!recognized_any)
        {
            LogWarning("Unhandled IPI mailbox payload: %u\n", ipi_payload);
        }
    }
    else if ((interrupt_source & BCM2837ARMLocalInterruptSources::GPU_INTERRUPT) != BCM2837ARMLocalInterruptSources::NONE)
    {
        Interrupts interrupt = AsInterrupt(static_cast<BCM2837Interrupts>(GetRegister(BCM2837ARMCInterruptRequestRegisters::REQUEST_PENDING_1)));

        if (interrupt == Interrupts::NO_SUCH_INTERRUPT)
        {
            LogWarning("Unhandled interrupt source: %d\n", interrupt_source);
        }
        else if (!DispatchInterruptType(interrupt, core_task_switch_isr))
        {
            LogError("No ISRs found for Interrupt: %s\n", ToString(interrupt));
        }
    }

    if (core_task_switch_isr != nullptr)
    {
        LogDebug1("Executing Core Task Switch ISR\n");
        core_task_switch_isr->HandleInterrupt();
    }

    EnableIRQs();
}
