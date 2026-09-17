// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi4/rpi4_platform_info.h"
#include "platform/address_space_layout.h"

RPI4PlatformInfo::RPI4PlatformInfo()
{
    GetPlatformDetails(GetMailboxRegisterBase());
}

RPIBoardType RPI4PlatformInfo::GetBoardType() const
{
    return RPIBoardType::RPI4;
}

const char *RPI4PlatformInfo::GetBoardTypeName() const
{
    return "Raspberry Pi 4B";
}

SchedulerClockSource RPI4PlatformInfo::GetSchedulerClockSource() const
{
    return SchedulerClockSource::ARM_GENERIC_TIMER;
}

EMMCControllerType RPI4PlatformInfo::GetEMMCControllerType() const
{
    //  EMMC2 and the legacy Arasan controller are both SDHCI 3.0 parts and take the same
    //      bring-up sequence -- only the base address differs.

    return EMMCControllerType::BCM2711_EMMC2;
}

uint8_t *RPI4PlatformInfo::GetARMLocalBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)ARM_LOCAL_BASE);
}

uint8_t *RPI4PlatformInfo::GetMMIOBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)BCM2711_IO_BASE);
}

uint8_t *RPI4PlatformInfo::GetMailboxRegisterBase() const
{
    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)BCM2711_MAILBOX_REGISTER_BASE);
}

uint8_t *RPI4PlatformInfo::GetEMMCBase() const
{
    const uint8_t *base = (GetHostType() == HostType::QEMU) ? BCM2711_LEGACY_EMMC_BASE : BCM2711_EMMC_BASE;

    return (uint8_t *)PhysicalToKernelVirtualAddress((uint64_t)base);
}

uint32_t RPI4PlatformInfo::GetGPUClockRate() const
{
    return BCM2711_SYSTEM_CLOCK;
}

uint32_t RPI4PlatformInfo::GetNumberOfCores() const
{
    return 4;
}
