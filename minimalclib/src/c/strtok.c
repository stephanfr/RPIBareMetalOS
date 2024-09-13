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

char *strtok(char *str, const char *delim, char **saveptr)
{
    if(str == NULL)
    {
        if( *saveptr == NULL)
        {
            return NULL;
        }

        str = *saveptr;
    }

    str += strspn(str, delim);

    if (*str == '\0')
    {
        *saveptr = NULL;
        return NULL;
    }

    size_t next_delim = strcspn(str, delim);

    *saveptr = str + next_delim;

    if (**saveptr != '\0')
    {
        **saveptr = '\0';
        (*saveptr)++;
    }
    else
    {
        *saveptr = NULL;
    }

    return str;
}
