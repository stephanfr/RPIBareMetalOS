/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "stddef.h"
#include "ctype.h"

int strnicmp(const char *string1, const char *string2, size_t count)
{
    if (count == 0)
    {
        return (0);
    }

    do
    {
        //  If the current characters are not equal, then we return with determining if string1 is less than or greater than string2

        char char1 = islower(*string1) ? toupper(*string1) : *string1;
        char char2 = islower(*string2) ? toupper(*string2++) : *string2++;

        if (char1 != char2)
        {
            return ((unsigned char)char1 - (unsigned char)char2);
        }

        //  If we are here, the characters are equal, so if string1 is zero, then so is string2 and they match.

        if (*string1++ == 0)
        {
            break;
        }
    } while (--count != 0);

    //  If we are here, we either broke for the end of the strings or ran out of characters

    return (0);
}
