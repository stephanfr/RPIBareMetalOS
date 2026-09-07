// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi4/rpi4_exception_manager.h"

#include "asm_utility.h"

bool BCM2711ExceptionManager::Initialize()
{
    LogEntryAndExit("Entering Initialize");

    return true;
}

bool BCM2711ExceptionManager::SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id)
{
    asm volatile("dsb sy" ::: "memory");
    *reinterpret_cast<volatile uint32_t *>(GICD_SGIR) =
        ((1u << core_id) << GICD_SGIR_CPU_TARGET_LIST__SHIFT) | (static_cast<uint32_t>(ipi_id) & 0x0F);
    return true;
}

bool BCM2711ExceptionManager::EnableInterrupt(Interrupts interrupt_to_enable, CoreList on_cores)
{
    LogEntryAndExit("Entering EnableInterrupt: %s on cores: 0x%08X", ToString(interrupt_to_enable), on_cores.Cores());

    BCM2711Interrupts bcm_2711_interrupt = GetBCM2711InterruptType(interrupt_to_enable);

    if (bcm_2711_interrupt != BCM2711Interrupts::NO_SUCH_INTERRUPT)
    {
        for (uint32_t current_core = 0; current_core < platform_info.GetNumberOfCores(); current_core++)
        {
            if (on_cores.Cores() & (1 << current_core))
            {
                Enable2711Interrupt(current_core, bcm_2711_interrupt);
            }
        }
    }

    return true;
}

bool BCM2711ExceptionManager::DisableInterrupt(Interrupts interrupt_to_disable, CoreList on_cores)
{
    LogEntryAndExit("Entering DisableInterrupt: %s on cores: 0x%08X", ToString(interrupt_to_disable), on_cores.Cores());

    BCM2711Interrupts bcm_2711_interrupt = GetBCM2711InterruptType(interrupt_to_disable);

    if (bcm_2711_interrupt != BCM2711Interrupts::NO_SUCH_INTERRUPT)
    {
        for (uint32_t current_core = 0; current_core < platform_info.GetNumberOfCores(); current_core++)
        {
            if (on_cores.Cores() & (1 << current_core))
            {
                Disable2711Interrupt(current_core, bcm_2711_interrupt);
            }
        }
    }

    return true;
}

void BCM2711ExceptionManager::HandleInterrupt()
{
    LogEntryAndExit("Entering HandleInterrupt");

    uint32_t irq_ack_reg = GetGICRegister(BCM2711GenericInterruptControllerRegisters::INTERRUPT_ACKNOWLEDGE);
    uint32_t irq = irq_ack_reg & 0x000003FF;

    InterruptServiceRoutine *core_task_switch_isr = nullptr;

    if (irq < 16)
    {
        //  SGI: intid == InterprocessorInterrupts value
        Interrupts interrupt = AsInterrupt(static_cast<InterprocessorInterrupts>(irq));
        DispatchInterruptType(interrupt, core_task_switch_isr);
    }
    else
    {
        Interrupts interrupt = GetInterruptType(irq);

        if (!DispatchInterruptType(interrupt, core_task_switch_isr))
        {
            LogError("No ISRs found for Interrupt: %u\n", irq);
        }
    }

    //  Let the GIC know we have serviced the interrupt.  End of interrupt ordering MUST mirror the acknowledge ordering,
    //      this needs to be enforced even with nested interrupts.

    SetGICRegister(BCM2711GenericInterruptControllerRegisters::END_OF_INTERRUPT, irq_ack_reg);

    if (core_task_switch_isr != nullptr)
    {
        LogDebug1("Executing Core Task Switch ISR\n");
        core_task_switch_isr->HandleInterrupt();
    }

    EnableIRQs();
}
