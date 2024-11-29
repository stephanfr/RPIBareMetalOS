// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <forward_list>
#include <heap_allocator>
#include <map>
#include <memory>

#include "heaps.h"
#include "processor_cores.h"

#include "isr/isr.h"

typedef enum class InterprocessorInterrupts : uint32_t
{
    NO_SUCH_IPI = 0,
    HALT = 1,
    CORE_TASK_SWITCH = 2
} InterprocessorInterrupts;


class ExceptionManager
{
public:
    virtual bool Initialize() = 0;

    virtual bool EnableInterrupt(Interrupts interrupt_to_enable, CoreList on_cores) = 0;
    virtual bool DisableInterrupt(Interrupts interrupt_to_disable, CoreList on_cores) = 0;

    virtual bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) = 0;

    virtual bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr, CoreList on_cores) = 0;

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
        asm volatile("msr    daifclr, #2"); //  Enables interrupts on the processor
    }

    ~ExceptionManager()
    {
        asm volatile("msr	daifset, #2"); //  Disables interrupts on the processor
    }

    bool AddISR(InterruptServiceRoutine *isr, CoreList on_cores);

    ISRPointerList *GetISRs(Interrupts interrupt_raised)
    {
        ISRMap::iterator map_itr = isrs_.find(interrupt_raised);

        if (map_itr == isrs_.end())
        {
            return nullptr;
        }

        return map_itr->second();
    }

    Interrupts AsInterrupt(InterprocessorInterrupts ipi)
    {
        switch (ipi)
        {
        case InterprocessorInterrupts::HALT:
            return Interrupts::CORE_HALT;
        case InterprocessorInterrupts::CORE_TASK_SWITCH:
            return Interrupts::SWITCH_TASK;
        default:
            return Interrupts::NO_SUCH_INTERRUPT;
        }

        return Interrupts::NO_SUCH_INTERRUPT;
    }
};

ExceptionManager &GetExceptionManager();
