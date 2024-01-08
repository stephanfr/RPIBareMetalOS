/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>

size_t strlcpy(char *destination, const char *source, size_t max_size)
{
    char *dest = destination;
    const char *src = source;
    size_t space_left = max_size;

    //  Copy as many bytes as will fit

    if (space_left != 0)
    {
        while (--space_left != 0)
        {
            if ((*dest++ = *src++) == 0)
            {
                break;
            }

        } ;
    }

    //  We have hit the max size for the destination string, so null terminate and we are done

    if (space_left == 0)
    {
        if (max_size != 0)
        {
            *dest = '\0'; //  Add null termination
        }

        while (*src++); //  find the end of the source string, as we return the equivalent of strlen(src)
    }

    //  Bytes copied does not include null termination

    return (src - source - 1);
}
