// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/gic/gic_exception_manager.h"

//
//  BCM2711 (RPi4) GICv2 exception manager.
//
//  GIC-400 distributor base: 0xFF841000
//  GIC-400 CPU interface base: 0xFF842000
//  System timer SPIs map to GIC INTIDs 96-99 (same as RPi5).
//

class BCM2711ExceptionManager : public gic_exception_manager
{
public:
    BCM2711ExceptionManager()
        : gic_exception_manager(GICD_BASE, GICC_BASE, SYSTEM_TIMER_SPI_BASE_INTID)
    {
    }


private:
    static constexpr uint64_t GICD_BASE                = 0xFF841000;
    static constexpr uint64_t GICC_BASE                = 0xFF842000;
    static constexpr uint32_t SYSTEM_TIMER_SPI_BASE_INTID = 96;
};
