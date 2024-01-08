// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi4/rpi4_platform_info.h"

RPI4PlatformInfo::RPI4PlatformInfo()
{
    GetPlatformDetails(GetMMIOBase());
}

const RPIBoardType RPI4PlatformInfo::GetBoardType() const
{
    return RPIBoardType::RPI4;
}

const char *RPI4PlatformInfo::GetBoardTypeName() const
{
    return "Raspberry Pi 4B";
}

uint8_t *RPI4PlatformInfo::GetMMIOBase() const
{
    return const_cast<uint8_t *>(BCM2711_IO_BASE);
}

uint8_t *RPI4PlatformInfo::GetEMMCBase() const
{
    return const_cast<uint8_t *>(BCM2711_EMMC_BASE);
}

const uint32_t RPI4PlatformInfo::GetGPUClockRate() const
{
    return BCM2711_SYSTEM_CLOCK;
}
