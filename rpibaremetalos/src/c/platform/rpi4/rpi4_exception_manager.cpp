// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi4/rpi4_exception_manager.h"

#include <minimalstdio.h>

#include "asm_utility.h"

bool BCM2711ExceptionManager::Initialize()
{
    LogEntryAndExit("Entering Initialize");

    return true;
}

bool BCM2711ExceptionManager::SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id)
{
//    printf("Sending IPI to core %d\n", core_id);
    *reinterpret_cast<uint32_t *>(GICD_SGIR) = ((uint32_t)1 << (core_id + GICD_SGIR_CPU_TARGET_LIST__SHIFT)) | static_cast<uint32_t>(ipi_id);
    return true;
}

void BCM2711ExceptionManager::HandleInterrupt()
{
    LogEntryAndExit("Entering HandleInterrupt");

//    printf("Handling Interrupt on core: %ld\n", GetCoreID());

    //    uint32_t interrupt_source = *(uint32_t volatile *)(0xff800060 + (core_id * 4));

    /*
        if ((interrupt_source & BCM2711ARMCoreInterruptSources::ANY_MAILBOX) != BCM2711ARMCoreInterruptSources::NONE)
        {

            printf("IPI Message Recieved in mailbox: %d for core %d\n", GetMailboxId(interrupt_source), core_id);

            uint32_t mailbox_0 = *(uint32_t volatile *)(GetPlatformInfo().GetARMLocalBase() + ARM_CORE_MAILBOX_READ_CLEAR_OFFSET + (16 * core_id));
            printf("Mailbox 0: %x\n", mailbox_0);
            *(uint32_t volatile *)(GetPlatformInfo().GetARMLocalBase() + ARM_CORE_MAILBOX_READ_CLEAR_OFFSET + (16 * core_id)) = 0xFFFFFFFF;

            return;
        }
    */

    unsigned int irq_ack_reg = GetGICRegister(BCM2711GenericInterruptControllerRegisters::INTERRUPT_ACKNOWLEDGE);
    unsigned int irq = irq_ack_reg & 0x2FF;

    ISRPointerList *isrs = GetISRs(GetInterruptType(irq));

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
            else if (current_isr->ISRType() == InterruptServiceRoutineType::IMPERATIVE_CORE_TASK_SWITCH)
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
        LogError("No ISRs found for Interrupt: %u\n", irq);
    }

    //  Let the GIC know we have serviced the interrupt.  End of interrupt ordering MUST mirror the acknowledge ordering,
    //      this needs to be enforced even with nested interrupts.

    SetGICRegister(BCM2711GenericInterruptControllerRegisters::END_OF_INTERRUPT, irq_ack_reg);
    EnableIRQ();

    //  Interrupt has been acknowledged and all other ISRs handled, execute the task scheduler now if we have one.

    if (task_switch_isr != nullptr)
    {
        LogDebug1("Executing Task Switch ISR\n");
        task_switch_isr->HandleInterrupt();
    }
    else if (core_task_switch_isr != nullptr)
    {
        LogDebug1("Executing Core Task Switch ISR\n");
        core_task_switch_isr->HandleInterrupt();
    }
}
