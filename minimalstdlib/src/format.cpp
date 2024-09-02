// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <format>

#include <ctype.h>

#include <fixed_string>

namespace MINIMAL_STD_NAMESPACE
{
    //
    //  fmt methods follow
    //

    bool fmt::ParseArgFormatString(const minstd::string &argument_format, arg_format_options &format_options)
    {
        format_options.clear();

        //  Default to integer base 10

        format_options.integer_base_ = 10;

        //  If the string is empty - return now

        if (argument_format.empty())
        {
            return true;
        }

        //  Search for the colon - if it is not the first character, then we have a positional argument

        size_t colon_position = argument_format.find(':');

        if (colon_position == argument_format.npos)
        {
            //  No colon, so this can only be a position specifier

            colon_position = argument_format.length();
        }

        //  If the colon is not the first character, then we have a positional argument, so extract it

        if (colon_position > 0)
        {
            format_options.position_ = 0;

            for (size_t i = 0; i < colon_position; i++)
            {
                if (!isdigit(argument_format[i]))
                {
                    //  Invalid positional argument

                    return false;
                }

                format_options.position_ = format_options.position_.value() * 10 + (argument_format[i] - '0');
            }
        }

        if (colon_position == argument_format.npos)
        {
            return true;
        }

        //  If there is nothing after the colon, then we are done

        size_t processed_characters = colon_position + 1;

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If the current character is a zero and the character after is not an alignment specifier,
        //      then we have a zero-prefixed format.  If the character is anything else,
        //      then we have a fill and alignment specifier.
        //
        //  We can handle zero prefixing with fill and alignment.

        bool has_zero_prefix = false;

        if ((argument_format[processed_characters] == '0') &&
            ((argument_format[processed_characters + 1] != '<') &&
             (argument_format[processed_characters + 1] != '>') &&
             (argument_format[processed_characters + 1] != '^')))
        {
            format_options.fill_ = '0';
            format_options.alignment_ = arg_format_options::align::right;
            processed_characters++;

            has_zero_prefix = true;
        }
        else
        {
            //  Look for an alignment specifier.

            size_t align_spec_loc = strcspn(argument_format.c_str() + processed_characters, "<>^");

            if ((processed_characters + align_spec_loc) < argument_format.length())
            {
                if ((align_spec_loc == 0) || (align_spec_loc == 1))
                {
                    switch (argument_format[processed_characters + align_spec_loc])
                    {
                    case '<':
                        format_options.alignment_ = arg_format_options::align::left;
                        break;

                    case '>':
                        format_options.alignment_ = arg_format_options::align::right;
                        break;

                    case '^':
                        format_options.alignment_ = arg_format_options::align::center;
                        break;
                    }
                }
                else
                {
                    //  Invalid alignment specifier

                    return false;
                }

                if (align_spec_loc == 1)
                {
                    format_options.fill_ = argument_format[processed_characters];
                }

                processed_characters += align_spec_loc + 1;
            }
        }

        //  If we have a hash sign, then we have the alt specifier

        if ((processed_characters < argument_format.length()) &&
            (argument_format[processed_characters] == '#'))
        {
            //  The alt specifier is incompatible with zero prefix

            if (has_zero_prefix)
            {
                return false;
            }

            format_options.alt_ = true;
            processed_characters++;
        }

        //  If we have a numeric character, then we have a width specifier

        while ((argument_format[processed_characters] != 0) &&
               (isdigit(argument_format[processed_characters])))
        {
            format_options.width_ = (format_options.width_.has_value() ? format_options.width_.value() * 10 : 0) + (argument_format[processed_characters] - '0');
            processed_characters++;
        }

        //  If we have a period, then we have a precision specifier

        if ((processed_characters < argument_format.length()) &&
            (argument_format[processed_characters] == '.'))
        {
            processed_characters++;

            while ((argument_format[processed_characters] != 0) &&
                   (isdigit(argument_format[processed_characters])))
            {
                format_options.precision_ = (format_options.precision_.has_value() ? format_options.precision_.value() * 10 : 0) + (argument_format[processed_characters] - '0');
                processed_characters++;
            }
        }

        //  If we have an alphabetic character, then we have a type specifier

        if ((processed_characters < argument_format.length()) &&
            (isalpha(argument_format[processed_characters])))
        {
            switch (argument_format[processed_characters])
            {
            case 'd':
                format_options.integer_base_ = 10;
                format_options.type_ = 'd';
                break;

            case 'x':
                format_options.integer_base_ = 16;
                format_options.type_ = 'x';
                break;

            case 'X':
                format_options.integer_base_ = 16;
                format_options.type_ = 'X';
                break;

            case 'b':
                format_options.integer_base_ = 2;
                format_options.type_ = 'b';
                break;

            case 'B':
                format_options.integer_base_ = 2;
                format_options.type_ = 'B';
                break;

            case 'o':
                format_options.integer_base_ = 8;
                format_options.type_ = 'o';
                break;

            default:
                format_options.type_ = argument_format[processed_characters];
            }
        }

        //  Finished

        return true;
    }

    void fmt::BuildOutput(minstd::string &buffer, const char *fmt, const fmt_arg *args[], size_t num_args)
    {
        minstd::fixed_string<32> argument_format;

        size_t format_placeholder_index = 0;

        size_t format_string_length = strnlen(fmt, SIZE_MAX); //  I don't like SiZE_MAX but we don't know the upper bound....

        size_t i = 0;

        while (fmt[i] != 0x00)
        {
            //  Look for an opening brace or the end of the format string.

            size_t opening_brace = strcspn(fmt + i, "{");

            buffer.append(fmt + i, opening_brace);
            i += opening_brace + 1;

            if (i >= format_string_length)
            {
                //  No more placeholders

                break;
            }

            //  Look for a closing brace or the end of the string

            size_t closing_brace = strcspn(fmt + i, "}");

            if (i + closing_brace >= format_string_length)
            {
                //  No closing brace, so exit now.

                break;
            }

            //  Extract the format string

            argument_format.clear();

            argument_format.append(fmt + i, closing_brace);

            i += closing_brace + 1;

            //  Parse the argument format string

            arg_format_options format_options;

            if (!ParseArgFormatString(argument_format, format_options))
            {
                //  Invalid format string

                buffer += "{Invalid format string: '";
                buffer += argument_format;
                buffer += "'}";
                continue;
            }

            //  Insert the argument

            if (format_options.position_.has_value())
            {
                if (format_options.position_.value() < num_args)
                {
                    args[format_options.position_.value()]->Append(buffer, format_options);
                }
            }
            else
            {
                if (format_placeholder_index < num_args)
                {
                    args[format_placeholder_index]->Append(buffer, format_options);

                    format_placeholder_index++;
                }
            }
        }
    }
} // namespace minstd
