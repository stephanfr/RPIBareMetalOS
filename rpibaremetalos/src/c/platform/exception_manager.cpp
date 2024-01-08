// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/exception_manager.h"

#include "platform/rpi3/rpi3_exception_manager.h"
#include "platform/rpi4/rpi4_exception_manager.h"

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
    "ERROR_INVALID_EL0_32"};

//
//  C linkage entry points for exceptions and interrupts
//

extern "C" void show_invalid_entry_message(unsigned int type, unsigned long esr, unsigned long address)
{
    GetExceptionManager().HandleException(type, esr, address);
}

extern "C" void handle_irq()
{
    GetExceptionManager().HandleInterrupt();
}

//
//  Methods for ExceptionManager follow
//

void ExceptionManager::HandleException(unsigned int type, unsigned long esr, unsigned long address)
{
    printf("%s, ESR: %x, address: %x\r\n", entry_error_messages[type], (unsigned int)esr, (unsigned int)address);
}

bool ExceptionManager::AddISR(InterruptServiceRoutine *isr)
{
    ISRMap::iterator map_itr = isrs_.find(isr->InterruptType());

    //  If we do not already have an ISR for the Interrupt, then we must add an entry to the map
    //      and enable the interrupt.
    //
    //  If the map entry alrady exists, we simply have to add the isr to the map's list.

    if (map_itr == isrs_.end())
    {
        auto insert_result = isrs_.insert(ISRMap::value_type(isr->InterruptType(), static_new<ISRPointerList>(list_allocator_)));

        if (!insert_result.second())
        {
            return false;
        }

        map_itr = insert_result.first();

        if (!EnableInterrupt(isr->InterruptType()))
        {
            isrs_.erase(map_itr);

            return false;
        }
    }

    //  Map already has a list for isrs, so push the isr onto the front of the list

    //  TODO - add a priority to ISRs

    map_itr->second()->push_front(isr);

    return true;
}