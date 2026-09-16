// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi3/rpi3_platform_info.h"
#include "platform/address_space_layout.h"

RPI3PlatformInfo::RPI3PlatformInfo()
{
    GetPlatformDetails(GetMailboxRegisterBase());
}

RPIBoardType RPI3PlatformInfo::GetBoardType() const
{
    return RPIBoardType::RPI3;
}

const char *RPI3PlatformInfo::GetBoardTypeName() const
{
    return "Raspberry Pi 3B";
}

uint8_t *RPI3PlatformInfo::GetARMLocalBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)ARM_LOCAL_BASE);
}

uint8_t *RPI3PlatformInfo::GetMMIOBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)BCM2837_IO_BASE);
}

uint8_t *RPI3PlatformInfo::GetMailboxRegisterBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)BCM2837_MAILBOX_REGISTER_BASE);
}

uint8_t *RPI3PlatformInfo::GetEMMCBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)BCM2837_EMMC_BASE);
}

uint32_t RPI3PlatformInfo::GetGPUClockRate() const
{
    return BCM2837_SYSTEM_CLOCK;
}

uint32_t RPI3PlatformInfo::GetNumberOfCores() const
{
    return 4;
}
