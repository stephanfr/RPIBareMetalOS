// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <forward_list>
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
    using ISRPointerListStaticHeapAllocator = minstd::pmr::polymorphic_allocator<ISRPointerList::node_type>;

    using ISRMap = minstd::map<Interrupts, ISRPointerList *>;
    using ISRMapStaticHeapAllocator = minstd::pmr::polymorphic_allocator<ISRMap::node_type>;

    ISRPointerListStaticHeapAllocator list_allocator_ = ISRPointerListStaticHeapAllocator(&__os_static_heap_resource);
    ISRMapStaticHeapAllocator map_allocator_ = ISRMapStaticHeapAllocator(&__os_static_heap_resource);

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

        return minstd::get<1>(*map_itr);
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

    //  Runs every registered ISR for 'interrupt', except for a single
    //      IMPERATIVE_CORE_TASK_SWITCH ISR, which is instead written into
    //      'core_task_switch_isr' (only if found) so the caller can defer
    //      it until every other ISR for this hardware interrupt has run --
    //      it may never return. Shared by every board's exception manager;
    //      returns false if nothing is registered for 'interrupt'.

    bool DispatchInterruptType(Interrupts interrupt, InterruptServiceRoutine *&core_task_switch_isr);

    //  A raw IPI mailbox payload may be the bitwise OR of multiple pending
    //      IPI values, since the mailbox register hardware accumulates
    //      every value written to it until it is explicitly cleared (e.g.
    //      HALT | CORE_TASK_SWITCH == 0b01 | 0b10 == 3, which does not
    //      equal any single InterprocessorInterrupts value). Every board
    //      uses the same core-mailbox mechanism for IPIs, so this decode
    //      is shared here instead of being duplicated (and independently
    //      getting this wrong) per board: invokes 'dispatch' once for each
    //      recognized IPI bit present in the payload. Returns true if at
    //      least one bit was recognized.

    template <typename DispatchFn>
    bool DispatchIPIMailboxPayload(uint32_t ipi_payload, DispatchFn &&dispatch)
    {
        bool dispatched_any = false;

        if ((ipi_payload & static_cast<uint32_t>(InterprocessorInterrupts::HALT)) != 0)
        {
            dispatch(AsInterrupt(InterprocessorInterrupts::HALT));
            dispatched_any = true;
        }

        if ((ipi_payload & static_cast<uint32_t>(InterprocessorInterrupts::CORE_TASK_SWITCH)) != 0)
        {
            dispatch(AsInterrupt(InterprocessorInterrupts::CORE_TASK_SWITCH));
            dispatched_any = true;
        }

        return dispatched_any;
    }
};

ExceptionManager &GetExceptionManager();
