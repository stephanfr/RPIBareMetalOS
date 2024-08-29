/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>
#include <string.h>

//
//  This implementation is based on the pretty standard table-lookup approach.
//      'char_bitset' is a bitset of 256 bits, one for each possible character value, initialized to zero.
//      Bits are set in the bitset for each character in the 'src' string.  Then, the 'dest' string
//      is scanned, and the bitset is used to determine if the character at the current location
//      is marked as absent from the bitset.  If it is, the scan continues, otherwise it stops.
//
//  Some implementations use a bitset of larger granularity, such as 32 or 64 bits per array element
//      to reduce the time reuired to zero the array.  Looking at code generation in Godbolt, it seems
//      that gcc generates the same code for 32-bit granularity as it does for 8-bit granularity.  Using
//      a byte array makes the code easier to read and understand.
//

size_t strcspn(const char *dest, const char *src)
{
    unsigned char char_bitset[32] = {0};

    //  Special case of src being empty

    if (src[0] == '\0')
    {
        return strnlen(dest, __SIZE_MAX__);
    }

    if (src[1] == '\0')
    {
        const char *dest_start = dest;

        for (; (*dest != 0x00) && (*dest != *src); dest++)
        {
        }

        return dest - dest_start;
    }

    for (unsigned char *src_itr = (unsigned char *)src; *src_itr; src_itr++)
    {
        char_bitset[*src_itr >> 3] |= ((unsigned char)0x01 << (*src_itr % 8));
    }

    unsigned char *dest_itr = (unsigned char *)dest;

    for (; (*dest_itr) && (!(char_bitset[*dest_itr >> 3] & ((unsigned char)0x01 << (*dest_itr % 8)))); dest_itr++)
    {
    }

    return dest_itr - (unsigned char *)dest;
};
