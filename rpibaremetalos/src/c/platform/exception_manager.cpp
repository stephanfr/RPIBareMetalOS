// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/exception_manager.h"

#include "platform/rpi3/rpi3_exception_manager.h"
#include "platform/rpi4/rpi4_exception_manager.h"

#include "asm_utility.h"

#include "devices/log.h"
#include "task/task_impl.h"

const char *entry_error_messages[] = {
    "SYNC_INVALID_EL1t",
    "IRQ_INVALID_EL1t",
    "FIQ_INVALID_EL1t",
    "ERROR_INVALID_EL1t",

    "SYNC_INVALID_EL1h",
    "IRQ_INVALID_EL1h",
    "FIQ_INVALID_EL1h",
    "ERROR_INVALID_EL1h",

    "SYNC_INVALID_EL0_64",
    "IRQ_INVALID_EL0_64",
    "FIQ_INVALID_EL0_64",
    "ERROR_INVALID_EL0_64",

    "SYNC_INVALID_EL0_32",
    "IRQ_INVALID_EL0_32",
    "FIQ_INVALID_EL0_32",
    "ERROR_INVALID_EL0_32",

    "SYNC_ERROR",
    "SYSCALL_ERROR"};

extern "C" void ShowInvalidExceptionTableEntryMessage(unsigned int type, unsigned long esr, unsigned long address, unsigned long far)
{
    GetExceptionManager().HandleException(type, esr, address, far);
}

extern "C" void HandleIRQ()
{
    GetExceptionManager().HandleInterrupt();
}

//  A fault taken at EL0 kills the TASK, not the core.  An EL1 fault is still fatal --
//      a kernel fault is a bug -- but a user program faulting is an ordinary event, and
//      the whole point of isolation is that the kernel survives it.
//
//  This runs on the faulting task's own kernel stack, on the frame KernelEntry EL0 built,
//      which is byte-for-byte the frame a syscall runs on.  That is why Exit() works here
//      unchanged: whatever makes it work for sc_Exit makes it work for this.  Exit() marks
//      the task a ZOMBIE and spins until the task switch takes it away, so this never returns.

extern "C" void HandleUserTaskFault(unsigned long esr, unsigned long elr, unsigned long far)
{
    task::TaskImpl &task = task::TaskImpl::GetTask();

    LogError("KILLED user task %s: ESR=%016lx PC=%016lx FAR=%016lx\n",
             task.Name().c_str(), esr, elr, far);

    task.Exit();
}

//
//  Methods for ExceptionManager follow
//

void ExceptionManager::HandleException(unsigned int type, unsigned long esr, unsigned long address, unsigned long far)
{
    LogError("%s, Core: %d ESR: %x, PC: %x, FAR: %x\r\n", entry_error_messages[type], GetCoreID(), (unsigned int)esr, (unsigned int)address, (unsigned int)far);
}

bool ExceptionManager::AddISR(InterruptServiceRoutine *isr, CoreList on_cores)
{
    LogEntryAndExit( "Adding ISR: %s on core: %d", isr->Name(), on_cores.Cores());

    ISRMap::iterator map_itr = isrs_.find(isr->InterruptType());

    //  If we do not already have an ISR for the Interrupt, then we must add an entry to the map
    //      and enable the interrupt.
    //
    //  If the map entry alrady exists, we simply have to add the isr to the map's list.

    if (map_itr == isrs_.end())
    {
        auto insert_result = isrs_.insert(ISRMap::value_type(isr->InterruptType(), static_new<ISRPointerList>(list_allocator_)));

        if (!minstd::get<1>(insert_result))
        {
            return false;
        }

        map_itr = minstd::get<0>(insert_result);

        if (!EnableInterrupt(isr->InterruptType(), on_cores))
        {
            isrs_.erase(map_itr);

            return false;
        }
    }

    //  Map already has a list for isrs, so push the isr onto the front of the list

    //  TODO - add a priority to ISRs

    minstd::get<1>(*map_itr)->push_front(isr);

    return true;
}

bool ExceptionManager::DispatchInterruptType(Interrupts interrupt, InterruptServiceRoutine *&core_task_switch_isr)
{
    ISRPointerList *isrs = GetISRs(interrupt);

    if (isrs == nullptr)
    {
        return false;
    }

    for (InterruptServiceRoutine *current_isr : *isrs)
    {
        if (current_isr->ISRType() == InterruptServiceRoutineType::IMPERATIVE_CORE_TASK_SWITCH)
        {
            core_task_switch_isr = current_isr;
        }
        else
        {
            current_isr->HandleInterrupt();
        }
    }

    return true;
}
