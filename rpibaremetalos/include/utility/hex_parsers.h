// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <cstdint>


inline int HexDigitValue(char c)
{
    if ((c >= '0') && (c <= '9')) return c - '0';
    if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
    if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
    return -1;
}

inline uint32_t ParseHexUint32(const char *text)
{
    if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
    {
        text += 2;
    }

    uint32_t value = 0;

    while (*text)
    {
        int digit = HexDigitValue(*text);

        if (digit < 0)
        {
            break;
        }

        value = (value << 4) | static_cast<uint32_t>(digit);
        text++;
    }

    return value;
}
