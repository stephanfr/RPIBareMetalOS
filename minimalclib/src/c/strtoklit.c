/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

//
//	strtok with the ability to handle quoted literals
//

char *strtoklit(char *str, const char *delim, const char *lit_delim, char **saveptr)
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
    size_t next_lit_delim = strcspn(str, lit_delim);

    if (next_delim <= next_lit_delim)
    {
        *saveptr = str + next_delim;
    }
    else
    {
        *saveptr = str + next_lit_delim;

        //  We have a literal, so determine the delimiter so we can find the end of the literal

        char current_lit_delimiter[] = {0, 0};

        for (int i = 0; i < strnlen(lit_delim, 128); i++)
        {
            if (**saveptr == lit_delim[i])
            {
                current_lit_delimiter[0] = lit_delim[i];
                break;
            }
        }

        //  We are at the start of the literal, move the rest of the string down one character to remove the leading literal delimiter

        memcpy(*saveptr, *saveptr + 1, strnlen(*saveptr, 65536));

        //  Find the closing delimiter or the end of the string

        *saveptr += strcspn(*saveptr, current_lit_delimiter);

        //  We are at the start of the literal, move the rest of the string down one character to remove the leading literal delimiter

        memcpy(*saveptr, *saveptr + 1, strnlen(*saveptr, 65536));

        if (**saveptr == '\0')
        {
            *saveptr = NULL;

            return str;
        }
        else
        {
            *saveptr = *saveptr + strcspn(*saveptr, delim);
        }
    }

    if (**saveptr != '\0')
    {
        *((*saveptr)++) = '\0';
    }
    else
    {
        *saveptr = NULL;
    }

    return str;
}
