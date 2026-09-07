// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/gic/gic_exception_manager.h"

//
//  BCM2712 (RPi5) GICv2 exception manager.
//
//  Register bases from bcm2712.dtsi:
//      gicv2: interrupt-controller@7fff9000 -- mapped into AArch64 space at
//          0x10_7FFF_9000 (distributor) and 0x10_7FFF_A000 (CPU interface).
//  System timer SPIs map to GIC INTIDs 96-99 (same as RPi4).
//

class RPI5ExceptionManager : public gic_exception_manager
{
public:
    RPI5ExceptionManager()
        : gic_exception_manager(GICD_BASE, GICC_BASE, SYSTEM_TIMER_SPI_BASE_INTID)
    {
    }

private:
    static constexpr uint64_t GICD_BASE                = 0x107FFF9000ULL;
    static constexpr uint64_t GICC_BASE                = 0x107FFFA000ULL;
    static constexpr uint32_t SYSTEM_TIMER_SPI_BASE_INTID = 96;
};