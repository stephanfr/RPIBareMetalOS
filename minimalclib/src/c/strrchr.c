/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>

char* strrchr( const char *str, int ch )
{
    char *search_position = (char*)str;
    char *last_found_at = NULL;

    while( *search_position )
    {
        if( *search_position == ch )
        {
            last_found_at = search_position;
        }

        search_position++;
    }

    return last_found_at;
}

