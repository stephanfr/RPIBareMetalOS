// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <format>

#include <ctype.h>
#include <string.h>

#include <fixed_string>

namespace MINIMAL_STD_NAMESPACE
{
    minstd::pair<bool, minstd::optional<uint32_t>> ParseArgId(const minstd::string &argument_format)
    {
        //  If the string is empty - return now

        if (argument_format.empty())
        {
            return minstd::make_pair(true, minstd::optional<uint32_t>());
        }

        //  Search for the colon - if it is not the first character or there is no colon at all,
        //      then we have a argument id.

        size_t colon_position = argument_format.find(':');

        colon_position = colon_position == argument_format.npos ? argument_format.length() : colon_position;

        if (colon_position == 0)
        {
            //  No id

            return minstd::make_pair(true, minstd::optional<uint32_t>());
        }

        //  If the colon is not the first character, then we have a id argument, so extract it

        uint32_t position = 0;

        for (size_t i = 0; i < colon_position; i++)
        {
            if (!isdigit(argument_format[i]))
            {
                //  Invalid id argument

                return minstd::make_pair(false, minstd::optional<uint32_t>());
            }

            position = position * 10 + (argument_format[i] - '0');
        }

        //  We have successfully extracted the id

        return minstd::make_pair(true, minstd::optional<uint32_t>(position));
    }

    bool ParseArgFormatString(const minstd::string &argument_format, arg_format_options &format_options)
    {
        format_options.clear();

        //  If the string is empty - return now

        if (argument_format.empty())
        {
            return true;
        }

        //  Skip the argument id if present

        size_t colon_position = argument_format.find(':');

        if (colon_position == argument_format.npos)
        {
            //  No colon, so this can only be an argument id.  We do nothing with this, so return now.

            return true;
        }

        //  Move forward to the colon and the start of the format spec

        size_t processed_characters = colon_position + 1;

        //  If there is nothing after the colon, then we are done

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  Extract the fill character and alignment specifier

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
                //  Fill character cannot be either an openin or closing curly brace.  We can only detect the opening brace here.

                if (argument_format[processed_characters] == '{')
                {
                    return false;
                }

                format_options.fill_ = argument_format[processed_characters];
            }

            processed_characters += align_spec_loc + 1;
        }

        //  Return if we are at the end of the string

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If we have a '+', '-', or ' ', then we have a sign specifier

        if ((argument_format[processed_characters] == '+') ||
            (argument_format[processed_characters] == '-') ||
            (argument_format[processed_characters] == ' '))
        {
            switch (argument_format[processed_characters])
            {
            case '+':
                format_options.sign_ = arg_format_options::sign::always_plus;
                break;

            case '-':
                format_options.sign_ = arg_format_options::sign::minus;
                break;

            case ' ':
                format_options.sign_ = arg_format_options::sign::space;
                break;
            }

            processed_characters++;
        }

        //  Return if we are at the end of the string

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If we have a hash sign, then we have the alt specifier

        if (argument_format[processed_characters] == '#')
        {
            format_options.alt_ = true;
            processed_characters++;
        }

        //  Return if we are at the end of the string

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If the next character is a zero, then we have the zero fill specifier

        if (argument_format[processed_characters] == '0')
        {
            format_options.zero_fill_ = !format_options.alignment_.has_value(); //  Zero fill is ignored with an alignment specifier

            processed_characters++;
        }

        //  Return if we are at the end of the string

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If we have a numeric character, then we have a width specifier

        while ((argument_format[processed_characters] != 0) &&
               (isdigit(argument_format[processed_characters])))
        {
            format_options.width_ = (format_options.width_.has_value() ? format_options.width_.value() * 10 : 0) + (argument_format[processed_characters] - '0');
            processed_characters++;
        }

        //  Return if we are at the end of the string

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If we have a period, then we have a precision specifier

        if (argument_format[processed_characters] == '.')
        {
            processed_characters++;

            while ((argument_format[processed_characters] != 0) &&
                   (isdigit(argument_format[processed_characters])))
            {
                format_options.precision_ = (format_options.precision_.has_value() ? format_options.precision_.value() * 10 : 0) + (argument_format[processed_characters] - '0');
                processed_characters++;
            }
        }

        //  Return if we are at the end of the string

        if (processed_characters >= argument_format.length())
        {
            return true;
        }

        //  If we have an alphabetic character, then we have a type specifier

        if (isalpha(argument_format[processed_characters]))
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

            case 'p':
                format_options.integer_base_ = 16;
                format_options.type_ = 'X'; //  Morph to hex spec
                break;

            default:
                format_options.type_ = argument_format[processed_characters];
            }
        }

        //  Finished

        return true;
    }

    //
    //  fmt methods follow
    //

    namespace internal
    {
        void BuildOutput(minstd::string &buffer, const char *fmt, const fmt_arg *args[], size_t num_args)
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

                //  Look for a closing brace or the end of the string.

                size_t closing_brace = strcspn(fmt + i, "}");

                if (i + closing_brace >= format_string_length)
                {
                    //  No closing brace, so exit now.

                    break;
                }

                //   Insure the closing brace is not where the fill character is expected

                if(( fmt[i + closing_brace + 1] == '<' ) ||
                   ( fmt[i + closing_brace + 1] == '>' ) ||
                   ( fmt[i + closing_brace + 1] == '^' ))
                {
                    buffer += "{Invalid format string: '";
                    buffer.append(fmt + i, closing_brace + 2);
                    buffer += "'}";
                    i += closing_brace + 2;
                    continue;
                }

                //  Extract the format string

                argument_format.clear();

                argument_format.append(fmt + i, closing_brace);

                i += closing_brace + 1;

                //  Look for a positional argument

                auto arg_position = ParseArgId(argument_format);

                if (!arg_position.first())
                {
                    //  Invalid format string

                    buffer += "{Invalid format string: '";
                    buffer += argument_format;
                    buffer += "'}";
                    continue;
                }

                //  Insert the argument.  If this is not a positional argument, we need to advance to the next argument.

                const uint32_t position = arg_position.second().has_value() ? arg_position.second().value() : format_placeholder_index++;

                if (position < num_args)
                {
                    args[position]->Append(buffer, argument_format);
                }
                else
                {
                    buffer += "{Argument position out of range}";
                }
            }
        }
    } // namespace internal
} // namespace minstd
