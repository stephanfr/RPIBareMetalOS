// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi3/rpi3_exception_manager.h"

#include <minimalstdio.h>

#include "asm_utility.h"

bool BCM2837ExceptionManager::Initialize()
{
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

    //  We have two different interrupt types to handle.  Core mailboxes are used for inter-processor interrupts
    //      and the GPU interrupt is used for system timer interrupts.  GPU interrupts are always routed to
    //      core 0, whereas core mailboxes are routed to the core written to when triggering the interrupt.
    //
    //  The local interrupt source register is read to determine the source of the interrupt, core mailbox zero
    //      for ipi interrupts and the GPU interrupt for GPU peripherals.
    //
    //  The different interrupt types are 'normalized' to a common Interrupts enum type to allow for a single
    //      ISR model for both interrupt types.

    uint32_t interrupt_source = GetCoreLocalInterruptSource(core_id);

    Interrupts interrupt = Interrupts::NO_SUCH_INTERRUPT;

    if ((interrupt_source & static_cast<BCM2837ARMLocalInterruptSources>((uint32_t)BCM2837ARMLocalInterruptSources::MAILBOX_0 << IPI_MAILBOX_ID) ) != BCM2837ARMLocalInterruptSources::NONE)
    {
        interrupt = ExceptionManager::AsInterrupt(static_cast<InterprocessorInterrupts>(GetCoreMailbox(core_id, IPI_MAILBOX_ID)));
        ResetCoreMailbox(core_id, IPI_MAILBOX_ID, 0xFFFFFFFF);   //  Reset the mailbox value otherwise the interrupt will be triggered again
    }
    else if ((interrupt_source & BCM2837ARMLocalInterruptSources::GPU_INTERRUPT) != BCM2837ARMLocalInterruptSources::NONE)
    {
        interrupt = AsInterrupt(static_cast<BCM2837Interrupts>(GetRegister(BCM2837ARMCInterruptRequestRegisters::REQUEST_PENDING_1)));
    }

    //  If we do not have an interrupt type, then return now.

    if (interrupt == Interrupts::NO_SUCH_INTERRUPT)
    {
        LogWarning("Unhandled interrupt source: %d\n", interrupt_source);
        return;
    }

    //  Get the ISRs for the interrupt type and execute

    ISRPointerList *isrs = GetISRs(interrupt);

    //  The task switch ISR is special as it may never return.  Therefore, trap it and we execute it last.

    InterruptServiceRoutine *task_switch_isr = nullptr;
    InterruptServiceRoutine *core_task_switch_isr = nullptr;

    if (isrs != nullptr)
    {
        for (InterruptServiceRoutine *current_isr : *isrs)
        {
            if (current_isr->ISRType() == InterruptServiceRoutineType::TASK_SCHEDULER)
            {
                task_switch_isr = current_isr;
            }
            else if(current_isr->ISRType() == InterruptServiceRoutineType::IMPERATIVE_CORE_TASK_SWITCH)
            {
                core_task_switch_isr = current_isr;
            }
            else
            {
                current_isr->HandleInterrupt();
            }
        }
    }
    else
    {
        LogError("No ISRs found for Interrupt: %s\n", ToString(interrupt));
    }

    //  Interrupt has been acknowledged and all other ISRs handled, execute the task scheduler now if we have one.

    if (task_switch_isr != nullptr)
    {
        LogDebug1("Executing Task Switch ISR\n");
        task_switch_isr->HandleInterrupt();
    }
    else if(core_task_switch_isr != nullptr)
    {
        LogDebug1("Executing Core Task Switch ISR\n");
        core_task_switch_isr->HandleInterrupt();
    }
}
