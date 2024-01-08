/**
 * Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stddef.h>

#include "internalsymbols.h"


char *__itoa( int value, char *str, int base )
{
    unsigned int    unsignedValue;
    unsigned int    firstCharOfNumber = 0;

    //  Insure the requested base is OK.  If not return NULL.

    if((base < 2) || (base > 36))
    { 
        str[0] = '\0';
        return NULL;
    }  

    //  Negative numbers are only supported for decimal.
    //      If negative, insert a minus sign into the first char of the output buffer, this will
    //      be reversed into the leading location by __utoa.
    
    if ((base == 10) && (value < 0))
    {              
        str[firstCharOfNumber++] = '-';
        unsignedValue = (unsigned int)-value;
    }
    else
    {
        unsignedValue = (unsigned int)value;
    }

    __utoa( unsignedValue, &str[firstCharOfNumber], base );

    return str;
}

char *itoa( int value, char *str, int base )
{
  return __itoa (value, str, base);
}
