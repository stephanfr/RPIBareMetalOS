// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <forward_list>
#include <heap_allocator>
#include <map>

#include "memory.h"

#include "isr/isr.h"

class ExceptionManager
{

public:
    virtual bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr) = 0;

    virtual void HandleInterrupt() = 0;

    void HandleException(unsigned int type, unsigned long esr, unsigned long address);

protected:
    using ISRPointerList = minstd::forward_list<InterruptServiceRoutine *>;
    using ISRPointerListStaticHeapAllocator = minstd::heap_allocator<ISRPointerList::node_type>;

    using ISRMap = minstd::map<Interrupts, ISRPointerList *>;
    using ISRMapStaticHeapAllocator = minstd::heap_allocator<ISRMap::node_type>;

    ISRPointerListStaticHeapAllocator list_allocator_ = ISRPointerListStaticHeapAllocator(__os_static_heap);
    ISRMapStaticHeapAllocator map_allocator_ = ISRMapStaticHeapAllocator(__os_static_heap);

    ISRMap isrs_ = ISRMap(map_allocator_);

    ExceptionManager()
    {
        asm volatile("msr    daifclr, #2");     //  Enables interrupts on the processor
    }

    ~ExceptionManager()
    {
        asm volatile("msr	daifset, #2");     //  Disables interrupts on the processor
    }

    virtual bool EnableInterrupt(Interrupts interrupt_to_enable) = 0;

    bool AddISR(InterruptServiceRoutine *isr);

    ISRPointerList *GetISRs(Interrupts interrupt_raised)
    {
        ISRMap::iterator map_itr = isrs_.find(interrupt_raised);

        if (map_itr == isrs_.end())
        {
            return nullptr;
        }

        return map_itr->second();
    }
};

ExceptionManager &GetExceptionManager();
