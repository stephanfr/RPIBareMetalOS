/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "stddef.h"

size_t strnlen(const char *str, size_t count)
{
    const char *start = str;

    while (count-- > 0 && *str)
    {
        str++;
    }

    return str - start;
}
