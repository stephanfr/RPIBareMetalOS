// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi5/rpi5_platform_info.h"

RPI5PlatformInfo::RPI5PlatformInfo()
{
    GetPlatformDetails(GetMailboxRegisterBase());
}

RPIBoardType RPI5PlatformInfo::GetBoardType() const
{
    return RPIBoardType::RPI5;
}

const char *RPI5PlatformInfo::GetBoardTypeName() const
{
    return "Raspberry Pi 5";
}

uint8_t *RPI5PlatformInfo::GetARMLocalBase() const
{
    return const_cast<uint8_t *>(BCM2712_ARM_LOCAL_BASE);
}

uint8_t *RPI5PlatformInfo::GetMMIOBase() const
{
    return const_cast<uint8_t *>(BCM2712_IO_BASE);
}

uint8_t *RPI5PlatformInfo::GetMailboxRegisterBase() const
{
    return const_cast<uint8_t *>(BCM2712_MAILBOX_REGISTER_BASE);
}

uint8_t *RPI5PlatformInfo::GetEMMCBase() const
{
    return const_cast<uint8_t *>(BCM2712_EMMC_BASE);
}

uint32_t RPI5PlatformInfo::GetGPUClockRate() const
{
    return BCM2712_SYSTEM_CLOCK;
}

uint32_t RPI5PlatformInfo::GetNumberOfCores() const
{
    return 4;
}