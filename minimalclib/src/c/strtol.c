/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <ctype.h>
#include <minimalstdclib_result_codes.h>
#include <limits.h>
#include <minimalcstdlib.h>
#include <stddef.h>
#include <stdbool.h>

long strtol(const char *numeric_string, char **first_nonconverted_character, int base, int *result_code)
{
    if( result_code != NULL )
    {
        result_code = MINIMALCLIB_NO_ERROR;
    }

    //  The base is limited to between 2 and 36 and base of zero means 'figure out the base from the first character'.

    if ((base < 0 ) || ( base == 1 ) || (base > 36))
    {
        if (first_nonconverted_character != 0)
        {
            *first_nonconverted_character = (char *)numeric_string;
        }

        if (result_code != NULL)
        {
            *result_code = MINIMALCLIB_EINVAL;
        }

        return 0;
    }

    const char *current_location_in_string = numeric_string;

    //  Skip leading whitespace

    int current_char;

    do
    {
        current_char = (unsigned char)*current_location_in_string++;
    } while (isspace(current_char));

    //  Look for a sign, either plus or minus

    bool negative_value;

    if (current_char == '-')
    {
        negative_value = true;
        current_char = *current_location_in_string++;
    }
    else
    {
        negative_value = false;

        if (current_char == '+')
        {
            current_char = *current_location_in_string++;
        }
    }

    //  If the base is zero or 16 and the value has a leading '0x' or 0X', then convert as hexadecimal

    if ((base == 0 || base == 16) && current_char == '0' &&
        (*current_location_in_string == 'x' || *current_location_in_string == 'X') && isxdigit((unsigned char)current_location_in_string[1]))
    {
        current_char = current_location_in_string[1];
        current_location_in_string += 2;
        base = 16;
    }

    //  If the base is still zero and the leading character is a zero, then convert as octal.
    //      Otherwise, set the base to 10.

    if (base == 0)
    {
        base = current_char == '0' ? 8 : 10;
    }

    //  Determine the maximum value that can be legally converted - this follows the C lib implementations of strtol
    //      closely as otherwise over and under flows will be different from the C lib standard.

    long cutoff;
    long cutlim;

    cutoff = negative_value ? LONG_MIN : LONG_MAX;
    cutlim = cutoff % base;
    cutoff /= base;

    if (negative_value)
    {
        if (cutlim > 0)
        {
            cutlim -= base;
            cutoff += 1;
        }

        cutlim = -cutlim;
    }

    bool under_or_overflow = false;
    long numeric_value = 0;

    while( true )
    {
        //  Compute the numeric ordinal for the current char

        if (isdigit(current_char))
        {
            current_char -= '0';
        }
        else if (isalpha(current_char))
        {
            current_char -= isupper(current_char) ? 'A' - 10 : 'a' - 10;
        }
        else
        {
            break;  //  Neither digit nor alpha
        }

        //  If the ordinal is beyond the base, then we are finished

        if (current_char >= base)
        {
            break;
        }

        //  If we have encountered an underflow or overflow error, just continue to consume characters 
        //      but do not add them to the return value

        if (under_or_overflow)
        {
            current_char = (unsigned char)*current_location_in_string++;
            continue;
        }

        //  Depending on sign, determine over or underflow and if not then add the new digit to the value.

        if (negative_value)
        {
            if (numeric_value < cutoff || (numeric_value == cutoff && current_char > cutlim))
            {
                under_or_overflow = true;
                numeric_value = LONG_MIN;

                if (result_code != NULL)
                {
                    *result_code = MINIMALCLIB_ERANGE;
                }
            }
            else
            {
                numeric_value *= base;
                numeric_value -= current_char;
            }
        }
        else
        {
            if (numeric_value > cutoff || (numeric_value == cutoff && current_char > cutlim))
            {
                under_or_overflow = true;
                numeric_value = LONG_MAX;

                if (result_code != NULL)
                {
                    *result_code = MINIMALCLIB_ERANGE;
                }
            }
            else
            {
                numeric_value *= base;
                numeric_value += current_char;
            }
        }

        current_char = (unsigned char)*current_location_in_string++;
    }

    if (first_nonconverted_character != NULL)
    {
        *first_nonconverted_character = (char *)( under_or_overflow ? numeric_string : current_location_in_string - 1 );
    }

    return numeric_value;
}
