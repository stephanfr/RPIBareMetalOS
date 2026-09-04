// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

#include <string.h>

uint32_t ParseHexUint32(const char *text)
{
    if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
    {
        text += 2;
    }

    uint32_t value = 0;

    while (*text)
    {
        char c = *text;
        int digit;

        if ((c >= '0') && (c <= '9')) digit = c - '0';
        else if ((c >= 'a') && (c <= 'f')) digit = c - 'a' + 10;
        else if ((c >= 'A') && (c <= 'F')) digit = c - 'A' + 10;
        else break;

        value = (value << 4) | static_cast<uint32_t>(digit);
        text++;
    }

    return value;
}

template<size_t BufferSize>
static bool ParseHexStringToFixedBuffer( const char *text,
                                         minstd::fixed_string<BufferSize> &out_buffer )
{
    if( text == nullptr )
    {
        return false;
    }

    if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
    {
        text += 2;
    }

    uint32_t position = 0;
    bool success = true;

    while ((*text != '\0') && (position < BufferSize - 1))
    {
        char c = *text;
        int digit;

        if ((c >= '0') && (c <= '9'))
        {
            digit = c - '0';
        }
        else if ((c >= 'a') && (c <= 'f'))
        {
            digit = c - 'a' + 10;
        }
        else if ((c >= 'A') && (c <= 'F'))
        {
            digit = c - 'A' + 10;
        }
        else
        {
            success = false;
            break;
        }

        // Directly convert hex digit to ASCII character
        out_buffer[position++] = static_cast<char>('0' + (digit > 9 ? digit - 10 : digit));
        text++;
    }

    out_buffer[position] = '\0';
    success = success && (position < BufferSize - 1);

    return success;
}
