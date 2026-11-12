// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/user_device_registry.h"

#include <string.h>

#include "uapi/user_devices.h"

namespace
{
    constexpr uint64_t PAGE_4K = 0x1000;

    struct BoardDevice
    {
        RPIBoardType board;
        UserDeviceDescriptor descriptor;
    };

    //  Every page here must also be on the board's stage-2 allow-list (hypervisor_memory.c).

    constexpr BoardDevice DEVICES[] = {
        {RPIBoardType::RPI3, {UserDeviceId::TEST_TIMER, DEVICE_NAME_TEST_TIMER, 0x3F003000, PAGE_4K, Interrupts::SYSTEM_TIMER_3}},
        {RPIBoardType::RPI4, {UserDeviceId::TEST_TIMER, DEVICE_NAME_TEST_TIMER, 0xFE003000, PAGE_4K, Interrupts::SYSTEM_TIMER_3}},
        {RPIBoardType::RPI5, {UserDeviceId::TEST_TIMER, DEVICE_NAME_TEST_TIMER, 0x107C003000, PAGE_4K, Interrupts::SYSTEM_TIMER_3}},

        {RPIBoardType::RPI3, {UserDeviceId::HW_RNG, DEVICE_NAME_BCM2835_RNG, 0x3F104000, PAGE_4K, Interrupts::NO_SUCH_INTERRUPT}},
        {RPIBoardType::RPI4, {UserDeviceId::HW_RNG, DEVICE_NAME_IPROC_RNG200, 0xFE104000, PAGE_4K, Interrupts::NO_SUCH_INTERRUPT}},
        {RPIBoardType::RPI5, {UserDeviceId::HW_RNG, DEVICE_NAME_IPROC_RNG200, 0x107D208000, PAGE_4K, Interrupts::NO_SUCH_INTERRUPT}},

        {RPIBoardType::RPI3, {UserDeviceId::CONSOLE_UART, DEVICE_NAME_CONSOLE_UART, 0x3F201000, PAGE_4K, Interrupts::UART0}},
        {RPIBoardType::RPI4, {UserDeviceId::CONSOLE_UART, DEVICE_NAME_CONSOLE_UART, 0xFE201000, PAGE_4K, Interrupts::UART0}},
        {RPIBoardType::RPI5, {UserDeviceId::CONSOLE_UART, DEVICE_NAME_CONSOLE_UART, 0x1F00030000, PAGE_4K, Interrupts::UART0}},

        //  The Pi 4's is EMMC2 on hardware; the kernel substitutes the legacy controller at 0xFE300000 under QEMU.

        {RPIBoardType::RPI3, {UserDeviceId::SD_CARD, DEVICE_NAME_SD_CARD, 0x3F300000, PAGE_4K, Interrupts::NO_SUCH_INTERRUPT}},
        {RPIBoardType::RPI4, {UserDeviceId::SD_CARD, DEVICE_NAME_SD_CARD, 0xFE340000, PAGE_4K, Interrupts::NO_SUCH_INTERRUPT}},
        {RPIBoardType::RPI5, {UserDeviceId::SD_CARD, DEVICE_NAME_SD_CARD, 0x1000FFF000, PAGE_4K, Interrupts::NO_SUCH_INTERRUPT}},
    };
}

bool FindUserDevice(RPIBoardType board, const char *name, UserDeviceDescriptor &descriptor)
{
    for (const BoardDevice &device : DEVICES)
    {
        if ((device.board == board) && (strncmp(device.descriptor.name, name, DEVICE_NAME_LENGTH) == 0))
        {
            descriptor = device.descriptor;
            return true;
        }
    }

    return false;
}

bool FindUserDevice(RPIBoardType board, UserDeviceId id, UserDeviceDescriptor &descriptor)
{
    for (const BoardDevice &device : DEVICES)
    {
        if ((device.board == board) && (device.descriptor.id == id))
        {
            descriptor = device.descriptor;
            return true;
        }
    }

    return false;
}
