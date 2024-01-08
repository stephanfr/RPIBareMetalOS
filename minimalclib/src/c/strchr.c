/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>

char* strchr( const char *str, int ch )
{
    char *search_position = (char*)str;

    while( *search_position && ( *search_position != ch ))
    {
        search_position++;
    }

    return *search_position == ch ? search_position : NULL;
}

