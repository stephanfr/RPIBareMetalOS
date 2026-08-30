// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/exception_manager.h"

#include "asm_utility.h"

#include <devices/log.h>

//
//  ARM GIC-400 (GICv2) driver for BCM2712 / RPi5.
//
//  Register bases come straight from bcm2712.dtsi:
//      gicv2: interrupt-controller@7fff9000 {
//          compatible = "arm,gic-400";
//          reg = <0x10 0x7fff9000 0x0 0x1000>,   // GICD (distributor)
//                <0x10 0x7fffa000 0x0 0x2000>,   // GICC (CPU interface)
//                ...
//
//  The distributor and every core's CPU interface are enabled in EL3 during
//      boot by ConfigureGIC400 (configure_gic.S) -- the same proven armstub
//      sequence used on RPi4, just with the RPi5 distributor base.  This class
//      only performs the runtime operations: enabling individual interrupt
//      sources, sending IPIs as Software Generated Interrupts (SGIs), and
//      acknowledging/ending interrupts via IAR/EOIR.
//
//  IPIs use SGIs (INTIDs 0-15), the standard GICv2 IPI mechanism.  The
//      InterprocessorInterrupts enum value doubles as the SGI INTID
//      (HALT = 1, CORE_TASK_SWITCH = 2), both safely inside the SGI range.
//

class RPI5ExceptionManager : public ExceptionManager
{
public:
    RPI5ExceptionManager() = default;

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

        //  SGIs (0-15) are always enabled on a GICv2 CPU interface -- nothing to do.

        if (intid < 16)
        {
            return true;
        }

        //  Priority 0 (highest), well below the 0xFF mask, so it is deliverable.

        reinterpret_cast<volatile uint8_t *>(GICD_BASE + GICD_IPRIORITYR)[intid] = 0x00;

        //  SPIs (>= 32) are routed to specific cores via ITARGETSR; PPIs (16-31)
        //      are per-core banked and ignore ITARGETSR.

        if (intid >= 32)
        {
            reinterpret_cast<volatile uint8_t *>(GICD_BASE + GICD_ITARGETSR)[intid] = static_cast<uint8_t>(on_cores.Cores() & 0xFF);
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

        //  SGIs cannot be disabled via ICENABLER; treat as a successful no-op.

        if (intid < 16)
        {
            return true;
        }

        GICD(GICD_ICENABLER + ((intid / 32) * 4)) = (1u << (intid % 32));

        return true;
    }

    bool SendInterprocessorInterrupt(uint32_t core_id, InterprocessorInterrupts ipi_id) override
    {
        //  GICD_SGIR: TargetListFilter[25:24] = 0 (use CPUTargetList),
        //      CPUTargetList[23:16] = target core bitmask, SGIINTID[3:0] = INTID.

        uint32_t sgi_intid = static_cast<uint32_t>(ipi_id) & 0x0F;
        uint32_t target_list = (1u << core_id) & 0xFF;

        //  Ensure all prior memory writes are visible before the target core wakes.

        asm volatile("dsb sy" ::: "memory");

        GICD(GICD_SGIR) = (target_list << GICD_SGIR_CPU_TARGET_LIST_SHIFT) | sgi_intid;

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
            LogError("RPI5 GIC: unhandled INTID: %u\n", intid);
        }

        //  End of interrupt -- write the full IAR value back (EOImode == 0, so this
        //      both drops priority and deactivates the interrupt).

        GICC(GICC_EOIR) = iar;

        //  A deferred core task switch ISR may never return, so run it last.

        if (core_task_switch_isr != nullptr)
        {
            core_task_switch_isr->HandleInterrupt();
        }
    }

private:
    //  GIC-400 (GICv2) register bases -- see bcm2712.dtsi (mapped as device memory
    //      by RPI5MemoryManager's L1 index 65 block covering 0x10_40000000-0x10_7FFFFFFF).

    static constexpr uint64_t GICD_BASE = 0x107FFF9000ULL; //  distributor
    static constexpr uint64_t GICC_BASE = 0x107FFFA000ULL; //  CPU interface

    //  Distributor register offsets

    static constexpr uint32_t GICD_IPRIORITYR = 0x400;
    static constexpr uint32_t GICD_ITARGETSR = 0x800;
    static constexpr uint32_t GICD_ISENABLER = 0x100;
    static constexpr uint32_t GICD_ICENABLER = 0x180;
    static constexpr uint32_t GICD_SGIR = 0xF00;
    static constexpr uint32_t GICD_SGIR_CPU_TARGET_LIST_SHIFT = 16;

    //  CPU interface register offsets

    static constexpr uint32_t GICC_IAR = 0x00C;
    static constexpr uint32_t GICC_EOIR = 0x010;

    static constexpr uint32_t GICC_IAR_INTID_MASK = 0x3FF;
    static constexpr uint32_t GIC_SPURIOUS_INTID = 1023;

    //  BCM system timer SPIs 64-67 map to GIC INTIDs 96-99 (GIC_SPI n => 32 + n).

    static constexpr uint32_t SYSTEM_TIMER_SPI_BASE_INTID = 96;

    static volatile uint32_t &GICD(uint32_t offset)
    {
        return *reinterpret_cast<volatile uint32_t *>(GICD_BASE + offset);
    }

    static volatile uint32_t &GICC(uint32_t offset)
    {
        return *reinterpret_cast<volatile uint32_t *>(GICC_BASE + offset);
    }

    //  Map an OS-level Interrupts value to a GIC INTID (-1 if unsupported).

    static int32_t InterruptToINTID(Interrupts interrupt)
    {
        switch (interrupt)
        {
        case Interrupts::CORE_HALT:
            return static_cast<int32_t>(InterprocessorInterrupts::HALT); //  SGI 1

        case Interrupts::SWITCH_TASK:
            return static_cast<int32_t>(InterprocessorInterrupts::CORE_TASK_SWITCH); //  SGI 2

        case Interrupts::SYSTEM_TIMER_0:
            return SYSTEM_TIMER_SPI_BASE_INTID + 0;

        case Interrupts::SYSTEM_TIMER_1:
            return SYSTEM_TIMER_SPI_BASE_INTID + 1;

        case Interrupts::SYSTEM_TIMER_2:
            return SYSTEM_TIMER_SPI_BASE_INTID + 2;

        case Interrupts::SYSTEM_TIMER_3:
            return SYSTEM_TIMER_SPI_BASE_INTID + 3;

        default:
            return -1;
        }
    }

    //  Map a received GIC INTID back to an OS-level Interrupts value.

    Interrupts INTIDToInterrupt(uint32_t intid)
    {
        if (intid < 16)
        {
            //  SGI -- the INTID equals the IPI enum value it was sent with.

            return AsInterrupt(static_cast<InterprocessorInterrupts>(intid));
        }

        if ((intid >= SYSTEM_TIMER_SPI_BASE_INTID) && (intid <= SYSTEM_TIMER_SPI_BASE_INTID + 3))
        {
            return static_cast<Interrupts>(static_cast<uint32_t>(Interrupts::SYSTEM_TIMER_0) + (intid - SYSTEM_TIMER_SPI_BASE_INTID));
        }

        return Interrupts::NO_SUCH_INTERRUPT;
    }
};