// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The devices a driver process may be given, per board: what sc_DeviceOpen looks a name up in.  Pure data and
//      lookup, so the host tests compile it.

#include <stdint.h>

#include "isr/isr.h"
#include "platform/board_type.h"

typedef enum class UserDeviceId : uint32_t
{
    TEST_TIMER = 0,
    HW_RNG,
    CONSOLE_UART,
    SD_CARD,

    COUNT
} UserDeviceId;

//  A bit per UserDeviceId.  A service task gets its set when it is forked, and holds it until it exits.

using DeviceCapabilities = uint32_t;

static_assert(static_cast<uint32_t>(UserDeviceId::COUNT) <= 32, "one capability bit per device");

constexpr DeviceCapabilities NO_DEVICE_CAPABILITIES = 0;

constexpr DeviceCapabilities CapabilityFor(UserDeviceId id)
{
    return DeviceCapabilities{1} << static_cast<uint32_t>(id);
}

struct UserDeviceDescriptor
{
    UserDeviceId id;
    const char *name;
    uint64_t registers_physical;        //  page aligned
    uint64_t registers_size;            //  whole pages, at most USER_DEVICE_SLOT
    Interrupts interrupt;               //  NO_SUCH_INTERRUPT when it has none
};

//  False when 'board' has no device of that name.

bool FindUserDevice(RPIBoardType board, const char *name, UserDeviceDescriptor &descriptor);

bool FindUserDevice(RPIBoardType board, UserDeviceId id, UserDeviceDescriptor &descriptor);
