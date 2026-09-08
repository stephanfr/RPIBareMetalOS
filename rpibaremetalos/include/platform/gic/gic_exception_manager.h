// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "asm_utility.h"

#include "platform/exception_manager.h"
#include "platform/address_space_layout.h"

#include <devices/log.h>

//
//  Shared GICv2 (GIC-400) exception manager base for RPi4 and RPi5.
//
//  Subclasses supply GICD and GICC base addresses and the system-timer SPI
//  base INTID via the protected constructor.  All GIC register access, IPI
//  dispatch, and interrupt routing logic lives here once.
//
//  IPIs use SGIs (INTIDs 0-15).  The InterprocessorInterrupts enum value
//  doubles as the SGI INTID: HALT=1, CORE_TASK_SWITCH=2.
//

class gic_exception_manager : public ExceptionManager
{
public:
    bool Initialize() override
    {
        return true;
    }

    bool AddInterruptServiceRoutine(InterruptServiceRoutine *isr, CoreList on_cores) override
    {
        return ExceptionManager::AddISR(isr, on_cores);
    }

    bool EnableInterrupt(Interrupts interrupt_to_enable, CoreList on_cores) override
    {
        int32_t intid = InterruptToINTID(interrupt_to_enable);

        if (intid < 0)
        {
            return false;
        }

        //  SGIs (0–15) are always enabled on a GICv2 CPU interface.

        if (intid < 16)
        {
            return true;
        }

        //  Priority 0 (highest), well below the 0xFF mask so it is deliverable.

        reinterpret_cast<volatile uint8_t *>(gicd_base_ + GICD_IPRIORITYR)[intid] = 0x00;

        //  SPIs (>= 32) are routed to specific cores via ITARGETSR;
        //  PPIs (16–31) are per-core banked and ignore ITARGETSR.

        if (intid >= 32)
        {
            reinterpret_cast<volatile uint8_t *>(gicd_base_ + GICD_ITARGETSR)[intid] =
                static_cast<uint8_t>(on_cores.Cores() & 0xFF);
        }

        GICD(GICD_ISENABLER + ((intid / 32) * 4)) = (1u << (intid % 32));

        return true;
    }

    bool DisableInterrupt(Interrupts interrupt_to_disable, CoreList on_cores) override
    {
        (void)on_cores;

        int32_t intid = InterruptToINTID(interrupt_to_disable);

        if (intid < 0)
        {
            return false;
        }

        //  SGIs cannot be disabled via ICENABLER -- treat as a successful no-op.

        if (intid < 16)
        {
            return true;
        }

        GICD(GICD_ICENABLER + ((intid / 32) * 4)) = (1u << (intid % 32));

        return true;
    }

    bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) override
    {
        //  GICD_SGIR: TargetListFilter[25:24]=0 (use CPUTargetList),
        //      CPUTargetList[23:16]=target core bitmask, SGIINTID[3:0]=INTID.

        asm volatile("dsb sy" ::: "memory");
        GICD(GICD_SGIR) = (((1u << core_id) & 0xFF) << GICD_SGIR_CPU_TARGET_LIST_SHIFT) |
                          (static_cast<uint32_t>(ipi_id) & 0x0F);

        return true;
    }

    void HandleInterrupt() override
    {
        uint32_t iar = GICC(GICC_IAR);
        uint32_t intid = iar & GICC_IAR_INTID_MASK;

        //  Spurious interrupt (1023) requires no EOI.

        if (intid == GIC_SPURIOUS_INTID)
        {
            return;
        }

        InterruptServiceRoutine *core_task_switch_isr = nullptr;
        Interrupts interrupt = INTIDToInterrupt(intid);

        if (interrupt != Interrupts::NO_SUCH_INTERRUPT)
        {
            DispatchInterruptType(interrupt, core_task_switch_isr);
        }
        else
        {
            LogError("GIC: unhandled INTID: %u\n", intid);
        }

        //  EOI drops priority and deactivates the interrupt (EOImode == 0).

        GICC(GICC_EOIR) = iar;

        //  A deferred core task switch ISR may never return -- run it last.

        if (core_task_switch_isr != nullptr)
        {
            core_task_switch_isr->HandleInterrupt();
        }
    }

protected:
    gic_exception_manager(uint64_t gicd_base, uint64_t gicc_base, uint32_t system_timer_spi_base)
        : gicd_base_(gicd_base), gicc_base_(gicc_base), system_timer_spi_base_(system_timer_spi_base)
    {
    }

private:
    uint64_t gicd_base_;
    uint64_t gicc_base_;
    uint32_t system_timer_spi_base_;

    //  GIC distributor register offsets

    static constexpr uint32_t GICD_IPRIORITYR                 = 0x400;
    static constexpr uint32_t GICD_ITARGETSR                  = 0x800;
    static constexpr uint32_t GICD_ISENABLER                  = 0x100;
    static constexpr uint32_t GICD_ICENABLER                  = 0x180;
    static constexpr uint32_t GICD_SGIR                       = 0xF00;
    static constexpr uint32_t GICD_SGIR_CPU_TARGET_LIST_SHIFT = 16;

    //  GIC CPU interface register offsets

    static constexpr uint32_t GICC_IAR                        = 0x00C;
    static constexpr uint32_t GICC_EOIR                       = 0x010;

    static constexpr uint32_t GICC_IAR_INTID_MASK             = 0x3FF;
    static constexpr uint32_t GIC_SPURIOUS_INTID              = 1023;

    
    volatile uint32_t &GICD(uint32_t offset)
    {
        return *reinterpret_cast<volatile uint32_t *>(PhysicalToKernelVirtualAddress(gicd_base_ + offset));
    }

    volatile uint32_t &GICC(uint32_t offset)
    {
        return *reinterpret_cast<volatile uint32_t *>(PhysicalToKernelVirtualAddress(gicc_base_ + offset));
    }

    int32_t InterruptToINTID(Interrupts interrupt)
    {
        switch (interrupt)
        {
        case Interrupts::CORE_HALT:
            return static_cast<int32_t>(InterprocessorInterrupts::HALT);

        case Interrupts::SWITCH_TASK:
            return static_cast<int32_t>(InterprocessorInterrupts::CORE_TASK_SWITCH);

        case Interrupts::SYSTEM_TIMER_0:
            return static_cast<int32_t>(system_timer_spi_base_ + 0);

        case Interrupts::SYSTEM_TIMER_1:
            return static_cast<int32_t>(system_timer_spi_base_ + 1);

        case Interrupts::SYSTEM_TIMER_2:
            return static_cast<int32_t>(system_timer_spi_base_ + 2);

        case Interrupts::SYSTEM_TIMER_3:
            return static_cast<int32_t>(system_timer_spi_base_ + 3);

        default:
            return -1;
        }
    }

    Interrupts INTIDToInterrupt(uint32_t intid)
    {
        if (intid < 16)
        {
            return AsInterrupt(static_cast<InterprocessorInterrupts>(intid));
        }

        if (intid >= system_timer_spi_base_ && intid <= system_timer_spi_base_ + 3)
        {
            return static_cast<Interrupts>(static_cast<uint32_t>(Interrupts::SYSTEM_TIMER_0) +
                                           (intid - system_timer_spi_base_));
        }

        return Interrupts::NO_SUCH_INTERRUPT;
    }
};
