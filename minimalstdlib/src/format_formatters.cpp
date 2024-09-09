// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <format>

#include <ctype.h>

#include <fixed_string>

namespace FMT_FORMATTERS_NAMESPACE
{
    using arg_format_options = ::MINIMAL_STD_NAMESPACE::arg_format_options;

    constexpr char LOWER_CASE_NUMERIC_CONVERSION_DIGITS[37] = "0123456789abcdefghijklmnopqrstuvwxyz";
    constexpr char UPPER_CASE_NUMERIC_CONVERSION_DIGITS[37] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    constexpr char PLUS_SIGN[] = {"+"};
    constexpr char MINUS_SIGN[] = {"-"};
    constexpr char SPACE_CHAR[] = {" "};
    constexpr char NO_SIGN_OR_SPACE[] = {""};

    //
    //  Function to handle integer prefixes for binary, octal or hex
    //

    size_t IntegerPrefixLength(const arg_format_options &format)
    {
        //  If this is hex, octal or binary and alt is pecified, then we need to add the prefix

        if (format.is_alt() && format.type_specifier().has_value())
        {
            switch (format.type_specifier().value())
            {
            case 'o':
                return 1;

            case 'b':
            case 'B':
            case 'x':
            case 'X':
                return 2;
            }
        }

        return 0;
    }

    void AddIntegerPrefix(minstd::string &buffer, const arg_format_options &format)
    {
        //  If this is hex, octal or binary and alt is pecified, then we need to add the prefix

        if (format.is_alt() && format.type_specifier().has_value())
        {
            switch (format.type_specifier().value())
            {
            case 'b':
                buffer.push_back('b');
                buffer.push_back('0');
                break;

            case 'B':
                buffer.push_back('B');
                buffer.push_back('0');
                break;

            case 'o':
                buffer.push_back('0');
                break;

            case 'x':
                buffer.push_back('x');
                buffer.push_back('0');
                break;

            case 'X':
                buffer.push_back('X');
                buffer.push_back('0');
                break;
            }
        }
    }

    const char* DetermineSign(bool is_negative, const arg_format_options &format)
    {
        //  If the value is negative, then we will always include the unary minus sign

        if (is_negative)
        {
            return MINUS_SIGN;
        }

        //  If the sign is specified, then we may need to add either a plus or space

        if (format.sign().has_value())
        {
            if (format.sign().value() == arg_format_options::sign_treatment::always_plus)
            {
                return PLUS_SIGN;
            }
            else if (format.sign().value() == arg_format_options::sign_treatment::space)
            {
                return SPACE_CHAR;
            }
        }

        //  If we are down here, then no sign or space before the number

        return NO_SIGN_OR_SPACE;
    }

    //
    //  Function to align and pad numerics
    //

    void HandleZeroFill(minstd::string &buffer, const char *sign, size_t start_of_number, const arg_format_options &format)
    {
        size_t number_length = (buffer.size() - start_of_number) + ( sign[0] != 0 ? 1 : 0 ) + IntegerPrefixLength(format);

        //  If the width is specified and the number of characters is less than the width and the
        //      alignment is either right or center or we have zero fill, then we need to add fill characters.

        if (format.width().has_value() && (format.width().value() > number_length))
        {
            //  Determine how much to fill in front of the number

            size_t fill_count = format.width().value() - number_length;

            for (size_t i = 0; i < fill_count; i++)
            {
                buffer.push_back('0');
            }
        }

        //  Add the sign and prefix.
        //      We do not do this above, because we only fill above if there is a width specified and it still needs to be met.

        AddIntegerPrefix(buffer, format);

        //  Add the sign

        buffer += sign;

        //	Reverse the string in place

        char c;

        size_t i = buffer.size() - 1;

        for (size_t j = start_of_number; j < i; j++, i--)
        {
            c = buffer[j];
            buffer[j] = buffer[i];
            buffer[i] = c;
        }
    }

    void HandleNumericAlignmentAndFill(minstd::string &buffer, const char* sign, size_t start_of_number, const arg_format_options &format)
    {
        //  Handle zero fill separately

        if (format.is_zero_fill())
        {
            HandleZeroFill(buffer, sign, start_of_number, format);
            return;
        }

        //  Format according to the alignment, fill and width

        size_t number_length = (buffer.size() - start_of_number) + ( sign[0] != 0 ? 1 : 0 ) + IntegerPrefixLength(format);

        //  Add the sign and if this is hex, octal or binary and alt is pecified, then we need to add the prefix

        AddIntegerPrefix(buffer, format);

        buffer += sign;

        //  If the width is specified and the number of characters is less than the width and the
        //      alignment is either right or center or we have zero fill, then we need to add fill characters.

        if (format.width().has_value() &&
            (format.width().value() > number_length) &&
            (format.is_right_aligned() || format.is_center_aligned()))
        {
            //  Determine how much to fill in front of the number

            size_t fill_count = format.width().value() - number_length;

            if (format.is_center_aligned())
            {
                fill_count /= 2;
            }

            for (size_t i = 0; i < fill_count; i++)
            {
                buffer.push_back(format.fill().value());
            }
        }

        //	Reverse the string in place

        char c;

        size_t i = buffer.size() - 1;

        for (size_t j = start_of_number; j < i; j++, i--)
        {
            c = buffer[j];
            buffer[j] = buffer[i];
            buffer[i] = c;
        }

        //  If the width is specified and the number of characters is less than the width
        //      then we have to add fill characters for either left or center alignment.

        number_length = (buffer.size() - start_of_number) + ( sign[0] != 0 ? 1 : 0 );

        if (format.width().has_value() && (format.width().value() > number_length))
        {
            size_t fill_count = format.width().value() - number_length;

            for (size_t i = 0; i < fill_count; i++)
            {
                buffer.push_back(format.fill().value());
            }
        }
    }

    //
    //  Template function to convert floats to string.  Works for all floating point types.
    //

    template <typename T>
    void FloatToString(minstd::string &buffer, const T &value, const arg_format_options &format)
    {
        static uint64_t const pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

        uint64_t decimal_multiplier = pow10[minstd::min(format.precision().value(), (uint32_t)8)];

        uint64_t decimals;
        uint64_t units;

        //  Start the conversion

        if (value < 0)
        {
            //  Negative numbers

            decimals = (uint64_t)(-value * decimal_multiplier) % decimal_multiplier;
            units = (uint64_t)(-value);
        }
        else
        {
            //  Positive numbers

            decimals = (uint64_t)(value * decimal_multiplier) % decimal_multiplier;
            units = (uint64_t)value;
        }

        size_t start_of_number = buffer.size();

        for (size_t i = 0; i < format.precision().value(); i++)
        {
            buffer.push_back((decimals % 10) + '0');
            decimals /= 10;
        }

        buffer.push_back('.');

        while (units > 0)
        {
            buffer.push_back((units % 10) + '0');
            units /= 10;
        }

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, DetermineSign((value < 0), format), start_of_number, format);
    }

    //
    //  Template function to convert unsigned integers to string.  Works for all unsigned integer types
    //      and leaves the string in reverse order.
    //

    template <typename T>
    void UnsignedIntToReversedString(minstd::string &buffer, T value, int base, const char *numeric_conversion_digits)
    {
        T remainder;

        //  Digits will be in reverse order as we convert

        do
        {
            remainder = value % base;
            buffer.push_back(numeric_conversion_digits[remainder]);
            value = value / base;
        } while (value != 0);
    }

    template <typename T>
    void UnsignedIntToString(minstd::string &buffer, T value, const arg_format_options &format)
    {
        //  Insure the desired base is legal, only 2 to 36 is supported

        if (!format.integer_base().has_value() ||
            ((format.integer_base().value() < 2) || (format.integer_base().value() > 36)))
        {
            buffer += "{Invalid base}";
            return;
        }

        //  Get the number in a reversed order

        size_t start_of_number = buffer.size();

        const char *numeric_conversion_digits = (format.type_specifier().has_value() && ((format.type_specifier().value() == 'X') || (format.type_specifier().value() == 'B'))) ? UPPER_CASE_NUMERIC_CONVERSION_DIGITS : LOWER_CASE_NUMERIC_CONVERSION_DIGITS;

        UnsignedIntToReversedString(buffer, value, format.integer_base().value(), numeric_conversion_digits);

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, minstd::fixed_string<4>(), start_of_number, format);
    }

    //
    //  Template function to convert signed integers to string.  Works for all signed integer types.
    //

    template <typename T>
    void SignedIntToString(minstd::string &buffer, T value, const arg_format_options &format)
    {
        //  Insure the requested base is OK

        if (!format.integer_base().has_value() ||
            ((format.integer_base().value() < 2) || (format.integer_base().value() > 36)))
        {
            buffer += "{Invalid base}";
            return;
        }

        //  We need to convert the value to an unsigned type to handle the negative case

        size_t start_of_number = buffer.size();

        const char *numeric_conversion_digits = (format.type_specifier().has_value() && ((format.type_specifier().value() == 'X') || (format.type_specifier().value() == 'B'))) ? UPPER_CASE_NUMERIC_CONVERSION_DIGITS : LOWER_CASE_NUMERIC_CONVERSION_DIGITS;

        typedef typename minstd::make_unsigned<T>::type unsigned_T;

        if (value < 0)
        {
            UnsignedIntToReversedString(buffer, (unsigned_T)-value, format.integer_base().value(), numeric_conversion_digits);
        }
        else
        {
            UnsignedIntToReversedString(buffer, (unsigned_T)value, format.integer_base().value(), numeric_conversion_digits);
        }

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, DetermineSign((value < 0), format), start_of_number, format);
    }

    //
    //  Template function to append strings to the buffer
    //

    template <typename T>
    void FormattedStringAppend(minstd::string &buffer, const T &value, size_t length, const arg_format_options &format_options)
    {
        //  Determine if fill is needed for alignment and if yes, the amount of fill needed before and/or after the string

        size_t fill_after_count = 0;
        size_t fill_before_count = 0;

        if (format_options.width().has_value() && (format_options.width().value() > length))
        {
            size_t fill_count = format_options.width().value() - length;

            if (format_options.alignment().has_value())
            {
                switch (format_options.alignment().value())
                {
                case arg_format_options::align::left:
                    fill_after_count = fill_count;
                    break;

                case arg_format_options::align::right:
                    fill_before_count = fill_count;
                    break;

                case arg_format_options::align::center:
                    fill_before_count = fill_count / 2;
                    fill_after_count = fill_count - fill_before_count;
                    break;
                }
            }
        }

        for (size_t i = 0; i < fill_before_count; i++)
        {
            buffer.push_back(format_options.fill().value());
        }

        buffer += value;

        for (size_t i = 0; i < fill_after_count; i++)
        {
            buffer.push_back(format_options.fill().value());
        }
    }

    //
    //  Specializations for specific argument formatters
    //

    template <>
    void fmt_arg_base<const char>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        FormattedStringAppend(buffer, value_, 1, format_options);
    }

    template <>
    void fmt_arg_base<const char *>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        FormattedStringAppend(buffer, value_, strnlen(value_, SIZE_MAX), format_options);
    }

    template <>
    void fmt_arg_base<const minstd::string &>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        FormattedStringAppend(buffer, value_, value_.length(), format_options);
    }

    template <>
    void fmt_arg_base<const uint64_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const int64_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const uint32_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const int32_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const uint16_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const int16_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const uint8_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const int8_t>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const float>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        FloatToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const double>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        FloatToString(buffer, value_, format_options);
    }

    template <>
    void fmt_arg_base<const bool>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        if (value_)
        {
            FormattedStringAppend(buffer, "true", 4, format_options);
        }
        else
        {
            FormattedStringAppend(buffer, "false", 5, format_options);
        }
    }

    template <>
    void fmt_arg_base<const void *>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, reinterpret_cast<uint64_t>(value_), format_options);
    }

} // namespace FMT_FORMATTERS_NAMESPACE
