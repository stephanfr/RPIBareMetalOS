/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

<<<<<<< HEAD
#include "scanf.h"
=======
#include "minimalstdio_constants.h"
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

#include <ctype.h>

#include <stdbool.h>

#include <string.h>
#include <minimalcstdlib.h>

#include "printf.h"

#define MAX_BUFFER_LENGTH 4096
#define MAX_MATCH_SET_LENGTH 32
#define NUMERIC_BUFFER_SIZE 32

typedef enum SscanfArgType
{
    SSCANF_ARG_NONE = 0,
    SSCANF_ARG_LONG
} SscanfArgType;

int __sscanf(const char *buffer, const char *format, ...)
{
    //  Scrub input values as best we can

    if ((buffer == NULL) || (strnlen(buffer, MAX_BUFFER_LENGTH) == 0))
    {
        return EOF;
    }

    if (format == NULL)
    {
        return EOF;
    }

    if (strnlen(format, 256) == 0)
    {
        return 0;
    }

    //  Setup variable arguments

    va_list varargs;
    va_start(varargs, format);

    //  Start parsing

    char numeric_buffer[NUMERIC_BUFFER_SIZE];

    int args_assigned = 0;

    const char *current_buffer_char = buffer;
    const char* current_format_char = format;

    while (*current_format_char)
    {
        //  If the current format character is not a percent sign, then it must match the input stream at this point exactly

        //  Consume whitespace in the format string

        bool    leading_spaces = isspace(*current_format_char);

        while (isspace(*current_format_char))
        {
            current_format_char++;
        }

        if( leading_spaces )
        {
            while( isspace(*current_buffer_char) )
            {
                current_buffer_char++;
            }
        }

        //  Any characters in the format string must match the buffer exactly, except for whitespace.

        while (*current_format_char != '%')
        {
            //  If we have whitespace, then break out of this loop and we wil return to the main
            //      loop to deal with the whitespace

            if(isspace(*current_format_char))
            {
                break;
            }

            //  Non-whitespace, so the character in the format string must match in the input string.

            if (*current_buffer_char != *current_format_char)
            {
                return args_assigned; //  No match, return now.
            }

            //  Advance the format string and the the input string;

            current_buffer_char++;
            current_format_char++;
        }

        //  If whitespace, then go back to the top of the loop and get the format string and the
        //      buffer to non-whitespace characters.

        if(isspace(*current_format_char))
        {
            continue;
        }

        //  If we are here, we have a percent sign.

        current_format_char++; //  Advance past the percent sign

        //  Check for the assignment suppression modifier.
        //      If the asterkisk follows the percent sign, we process the format specifier but do not assign to an argument

        bool assign_argument = true;

        if (*current_format_char == '*')
        {
            assign_argument = false;
            current_format_char++;
        }

        //  Determine if the max field width has been specified

        unsigned max_field_width = 0;
        bool limit_field_width = false;

        while (isdigit(*current_format_char))
        {
            limit_field_width = true;
            max_field_width *= 10;
            max_field_width += (*current_format_char - '0');
            current_format_char++;
        }

        //  Check for a size modifier

        SscanfArgType   argument_type = SSCANF_ARG_NONE;

        if( *current_format_char == 'l' )
        {
            argument_type = SSCANF_ARG_LONG;
            current_format_char++;
        }

        //  Determine if we have a closed set of characters to capture

        char match_set[MAX_MATCH_SET_LENGTH];
        unsigned match_set_length = 0;
        bool member_of_set = true;

        if (*current_format_char == '[')
        {
            current_format_char++;

            //  Detrmine if we are inverting the set membership

            if (*current_format_char == '^')
            {
                member_of_set = false;
                current_format_char++;
            }

            //  If the first character is the closing square brace, then it is in the set

            if (*current_format_char == ']')
            {
                match_set[match_set_length] = *current_format_char;
                match_set_length++;
                current_format_char++;
            }

            //  The set contains everything up to the closing square brace

            while (*current_format_char != ']')
            {
                //  If we hit the end of the string, then we have an error condition

                if( *current_format_char == '\0' )
                {
                    return EOF;
                }

                if (match_set_length < MAX_MATCH_SET_LENGTH)
                {
                    match_set[match_set_length] = *current_format_char;
                    match_set_length++;
                }

                current_format_char++;
            }

            //  The set must be non-empty.  This is an imperfect test at best, due to the complication of the sscanf allowing
            //      a closing square brace as the first character of the set.  There is not a clear way to determine if
            //      there is truly an empty set or that the closing square brace is simply part of the set.  In practice,
            //      a poorly formed set with the square brace in the set will lead to really incorrect behavior.
            //
            //  I don't think there is a way to actually trip the test below - but it is included for completeness.

            if (( match_set_length == 0 ) || (( match_set_length == 1 ) && ( match_set[0] == ']' )))
            {
                return EOF;
            }
        }

        //  Process the conversion specifiers

        switch (*current_format_char)
        {
            //  Convert a single character - even if it is whitespace

        case 'c':
        {
            //  If we are capturing the argument, then make the assignment

            if (assign_argument)
            {
                *(char *)va_arg(varargs, char *) = *current_buffer_char;
                args_assigned++;
            }

            current_buffer_char++;
        }
        break;

            //  Numeric conversions

        case 'u':
        case 'd':
        case 'o':
        case 'x':
        case 'X':
        case 'i':
        {
            int base = 0;

            switch(*current_format_char)
            {
                case 'u':
                case 'd':
                    base = 10;
                    break;

                case 'o':
                    base = 8;
                    break;

                case 'x':
                case 'X':
                    base = 16;
                    break;

                case 'i':
                    base = 0;
                    break;                    
            }

            char* numeric_buffer_loc = numeric_buffer;
            const char *starting_buffer_loc = current_buffer_char;

            while( isspace(*current_buffer_char) )
            {
                *numeric_buffer_loc++ = *current_buffer_char++;
            }

            if( *current_buffer_char == '-' || *current_buffer_char == '+'  )
            {
                *numeric_buffer_loc++ = *current_buffer_char++;
            }

            while ( isalnum( *current_buffer_char ) && (numeric_buffer_loc - numeric_buffer < NUMERIC_BUFFER_SIZE) && ( !limit_field_width || ( max_field_width > 0)))
            {
                *numeric_buffer_loc++ = *current_buffer_char++;
                max_field_width--;
            }

            *numeric_buffer_loc++ = '\0';

            long value = 0;
            char *ending_char;
            int  result_code = -1;

            value = strtol( numeric_buffer, &ending_char, base, &result_code );

            current_buffer_char = starting_buffer_loc + ( ending_char - numeric_buffer );

            //  If we are capturing the value, set the argument

            if (assign_argument)
            {
                if( argument_type == SSCANF_ARG_NONE )
                {
                    *(int *)va_arg(varargs, int *) = value;
                }
                else if( argument_type == SSCANF_ARG_LONG )
                {
                    *(long *)va_arg(varargs, long *) = value;
                }

                args_assigned++;
            }
        }
        break;

        case ']':
        {
            //  Set the string argument from the argument list - or NULL if we are not capturing this argument

            char *string_arg = assign_argument ? va_arg(varargs, char *) : NULL;

            //  Ignore any leading whitespace

            while (isspace(*current_buffer_char))
            {
                current_buffer_char++;
            }

            //  We consume everything, including potentially whitespace if it is in the set

            while (true)
            {
                //  Check for the character in the set or not.  Break if the membership condition is violated

                bool convert_character = !member_of_set;

                for (unsigned i = 0; i < match_set_length; i++)
                {
                    if (match_set[i] == *current_buffer_char)
                    {
                        convert_character = member_of_set;
                        break;
                    }
                }

                if( !convert_character )
                {
                    break;
                }

                if (!limit_field_width || max_field_width > 0)
                {
                    if (assign_argument)
                    {
                        *string_arg = *current_buffer_char;
                        string_arg++;
                    }

                    current_buffer_char++;
                    max_field_width--;
                }
                else
                {
                    break; //  we have hit the end of the width limt, so break from the loop
                }
            }

            //  NULL terminate the string

            if (assign_argument)
            {
                *string_arg = '\0';
                args_assigned++;
            }
        }
        break;

        case 's':
        {
            //  Set the string argument from the argument list - or NULL if we are not capturing this argument

            char *string_arg = assign_argument ? va_arg(varargs, char *) : NULL;

            //  Ignore any leading whitespace

            while (isspace(*current_buffer_char))
            {
                current_buffer_char++;
            }

            //  Convert the charcters based on the following rules:
            //
            //  If this is a %s, then convert until a space is encountered or the max length is reached
            //  If this is a %[], then convert until a charcter not in the set is encounteredd or max length is reached
            //      - the caret symbol modifies processing to 'not in the set'

            while (true)
            {
                if (!isspace(*current_buffer_char) && (!limit_field_width || max_field_width > 0))
                {
                    if (assign_argument)
                    {
                        *string_arg = *current_buffer_char;
                        string_arg++;
                    }

                    current_buffer_char++;
                    max_field_width--;
                }
                else
                {
                    break; //  We encountered a space or hit the max length for the string
                }
            }

            //  NULL terminate the string

            if (assign_argument)
            {
                *string_arg = '\0';
                args_assigned++;
            }
        }
        break;
        }

        //  Advance to the next character in the format specifier

        current_format_char++;
    }

    //  finish with the varargs

    va_end(varargs);

    //  Reurn the number of arguments actually assigned

    return args_assigned;
}
