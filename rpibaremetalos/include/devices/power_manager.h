// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/platform_info.h"

#include "devices/system_timer.h"

#include "minimalstdio.h"

class PowerManager
{
public:
    PowerManager()
        : platform_info(GetPlatformInfo())
    {
    }

    void Halt();
    void Reboot();

private:
    typedef enum class PowerManagerRegisters
    {
        RSTC = 0x0010001C,
        RSTS = 0x00100020,
        WDOG = 0x00100024,

        PADS0 = 0x0010002C, // GPIO 0 - 27
        PADS1 = 0x00100030, // GPIO 28 - 45
        PADS2 = 0x00100034  // GPIO 46 - 53
    } PowerManagerRegisters;

    const uint32_t PM_PASSWD = 0x5A000000;
    const uint32_t PM_RSTC_CLEAR = 0xFFFFFFCF;
    const uint32_t PM_RSTC_REBOOT = 0x00000020;
    const uint32_t PM_RSTC_RESET = 0x00000102;
    const uint32_t PM_RSTS_PART_CLEAR = 0xFFFFFAAA;
    const uint32_t PM_WDOG_TIME = 0x000FFFFF;

    const PlatformInfo &platform_info;

    uint32_t GetRegister(PowerManagerRegisters reg)
    {
        return *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg));
    }

    void SetRegister(PowerManagerRegisters reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg)) = value;
    }
};
