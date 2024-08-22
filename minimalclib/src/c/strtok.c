/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>
#include <string.h>

//
//	Pretty simple implementation, search for the first delimiter in the string, replace it with a null terminator
//	and then search for the next non-delimiter character.
//

char *strtok(char *str, const char *delim)
{
    static char *buffer;

    if ((str == NULL) && ((str = buffer) == NULL))
    {
        return NULL;
    }

    str += strspn(str, delim);

    if (*str == '\0')
    {
        buffer = NULL;
        return NULL;
    }

    buffer = str + strcspn(str, delim);

    if (*buffer != '\0')
    {
        *buffer++ = '\0';
    }
    else
    {
        buffer = NULL;
    }

    return str;
}
