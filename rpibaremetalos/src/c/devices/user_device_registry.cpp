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
