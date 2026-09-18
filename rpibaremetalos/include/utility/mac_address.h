// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <stdint.h>
#include <string.h>

#include <array>
#include <format_formatters>

//
//  A MAC (IEEE 802) address - six bytes held in wire order, byte zero first.
//

class MACAddress
{
public:
    static constexpr size_t MAC_ADDRESS_LENGTH = 6;
    static constexpr size_t MAC_ADDRESS_STRING_LENGTH = 17;                                 //  "xx:xx:xx:xx:xx:xx"
    static constexpr size_t MAC_ADDRESS_STRING_BUFFER_SIZE = MAC_ADDRESS_STRING_LENGTH + 2; //  Pad a bit for the trailing null

    typedef char ToStringBuffer[MAC_ADDRESS_STRING_BUFFER_SIZE];

    MACAddress()
        : address_{0, 0, 0, 0, 0, 0}
    {
    }

    explicit MACAddress(const uint8_t bytes[MAC_ADDRESS_LENGTH])
    {
        memcpy(address_, bytes, MAC_ADDRESS_LENGTH);
    }

    explicit MACAddress(const minstd::array<uint8_t, MAC_ADDRESS_LENGTH> &bytes)
    {
        memcpy(address_, bytes.data(), MAC_ADDRESS_LENGTH);
    }

    MACAddress(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5)
        : address_{byte0, byte1, byte2, byte3, byte4, byte5}
    {
    }

    MACAddress(const MACAddress &address_to_copy) = default;
    MACAddress &operator=(const MACAddress &address_to_copy) = default;

    static const MACAddress NULL_ADDRESS;

    static bool FromString(const char *text, MACAddress &address);

    uint8_t operator[](size_t index) const
    {
        return address_[index];
    }

    const uint8_t *Bytes() const noexcept
    {
        return address_;
    }

    //  C++20 synthesizes operator!= from operator==

    bool operator==(const MACAddress &address) const
    {
        return memcmp(address_, address.address_, MAC_ADDRESS_LENGTH) == 0;
    }

    char *ToString(char buffer[MAC_ADDRESS_STRING_BUFFER_SIZE]) const;

private:
    uint8_t address_[MAC_ADDRESS_LENGTH];
};

static_assert(sizeof(MACAddress) == MACAddress::MAC_ADDRESS_LENGTH, "MACAddress must be exactly six bytes with no padding");

namespace FMT_FORMATTERS_NAMESPACE
{
    DECLARE_TYPE_FORMATTER(const MACAddress&, MACAddressFormatter, DEFAULT_STRING_FORMAT)
}
