// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The kernel's channels, each served by at most one driver process (task/channels.h).

#include <stddef.h>
#include <stdint.h>

#include "uapi/channels.h"

typedef enum class ChannelId : uint32_t
{
    TEST_ECHO = 0,

    COUNT
} ChannelId;

//  A bit per ChannelId: the channels a service task may serve.  Set when it is forked, held until it exits.

using ChannelCapabilities = uint32_t;

static_assert(static_cast<uint32_t>(ChannelId::COUNT) <= 32, "one capability bit per channel");

constexpr ChannelCapabilities NO_CHANNEL_CAPABILITIES = 0;

constexpr ChannelCapabilities CapabilityFor(ChannelId id)
{
    return ChannelCapabilities{1} << static_cast<uint32_t>(id);
}

//  The bytes of bulk data a request or reply can carry beside its message: whole pages, at most CHANNEL_DATA_MAX.

constexpr size_t ChannelDataSize(ChannelId id)
{
    switch (id)
    {
    case ChannelId::TEST_ECHO:
        return 0x10000;

    default:
        return 0;
    }
}
