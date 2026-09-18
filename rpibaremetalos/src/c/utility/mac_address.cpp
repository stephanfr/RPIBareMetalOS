// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "utility/mac_address.h"

#include "utility/hex_parsers.h"

#include <minimalstdio.h>

const MACAddress MACAddress::NULL_ADDRESS;

//  Parsing is deliberately hand-rolled rather than sscanf("%hhx:...") -- this
//      minimal libc's format-specifier support isn't something to guess at.

bool MACAddress::FromString(const char *text, MACAddress &address)
{
    if (text == nullptr)
    {
        return false;
    }

    //  Parse into a local so a malformed address never leaves the caller's
    //      address partially overwritten.

    uint8_t parsed[MAC_ADDRESS_LENGTH];

    for (size_t i = 0; i < MAC_ADDRESS_LENGTH; i++)
    {
        //  The second digit is only looked at if the first one is valid, so a
        //      string ending mid-octet never reads past its terminating null.

        int high = HexDigitValue(text[0]);
        int low = (high >= 0) ? HexDigitValue(text[1]) : -1;

        if (low < 0)
        {
            return false;
        }

        parsed[i] = static_cast<uint8_t>((high << 4) | low);

        text += 2;

        if (i < MAC_ADDRESS_LENGTH - 1)
        {
            if (*text != ':')
            {
                return false;
            }

            text += 1;
        }
    }

    //  Anything after the sixth octet means this was not an address.

    if (*text != '\0')
    {
        return false;
    }

    memcpy(address.address_, parsed, MAC_ADDRESS_LENGTH);

    return true;
}

char *MACAddress::ToString(char buffer[MAC_ADDRESS_STRING_BUFFER_SIZE]) const
{
    sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x", address_[0], address_[1], address_[2], address_[3], address_[4], address_[5]);

    return buffer;
}

namespace FMT_FORMATTERS_NAMESPACE
{
    template <>
    void fmt_arg_base<const MACAddress &>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        char mac_address_buffer[MACAddress::MAC_ADDRESS_STRING_BUFFER_SIZE];

        value_.ToString(mac_address_buffer);

        FormattedStringAppend(buffer, mac_address_buffer, MACAddress::MAC_ADDRESS_STRING_LENGTH, format_options);
    }
}
