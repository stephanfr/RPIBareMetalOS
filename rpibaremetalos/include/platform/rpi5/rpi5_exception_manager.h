// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/gic/gic_exception_manager.h"
#include "platform/rpi5/rp1_interrupts.h"

//
//  BCM2712 (RPi5) GICv2 exception manager.
//
//  Register bases from bcm2712.dtsi:
//      gicv2: interrupt-controller@7fff9000 -- mapped into AArch64 space at
//          0x10_7FFF_9000 (distributor) and 0x10_7FFF_A000 (CPU interface).
//  System timer SPIs map to GIC INTIDs 96-99 (same as RPi4).
//  RP1 peripherals arrive through MIP0 as edge SPIs at INTID 160 + RP1 source (RP1InterruptController).
//

class RPI5ExceptionManager : public gic_exception_manager
{
public:
    RPI5ExceptionManager()
        : gic_exception_manager(GICD_BASE, GICC_BASE, SYSTEM_TIMER_SPI_BASE_INTID, PERIPHERALS, PERIPHERAL_COUNT)
    {
    }

    //  Also brings up RP1 interrupt delivery.  A failure there leaves RP1 devices polled; it does not fail this.

    bool Initialize() override;

private:
    static constexpr uint64_t GICD_BASE                = 0x107FFF9000ULL;
    static constexpr uint64_t GICC_BASE                = 0x107FFFA000ULL;
    static constexpr uint32_t SYSTEM_TIMER_SPI_BASE_INTID = 96;

    static constexpr uint32_t PERIPHERAL_COUNT = 1;
    static constexpr GICPeripheralInterrupt PERIPHERALS[PERIPHERAL_COUNT] = {
        {Interrupts::UART0, RP1InterruptController::GIC_INTID_BASE + RP1InterruptController::UART0_SOURCE, InterruptTrigger::EDGE},
    };

    bool RP1DeliverySelfTest(RP1InterruptController &rp1);

    void AfterDispatch(uint32_t intid) override
    {
        if (RP1InterruptController::IsRP1INTID(intid))
        {
            GetRP1InterruptController().Acknowledge(intid);
        }
    }
};
