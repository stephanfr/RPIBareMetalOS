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

    //
    //  Function to handle integer prefixes for binary, octal or hex
    //

    void HandleIntegerPrefix(minstd::string &buffer, const arg_format_options &format)
    {
        //  If this is hex, octal or binary and alt is pecified, then we need to add the prefix

        if (format.alt_.has_value() && format.alt_.value() && format.type_.has_value())
        {
            switch (format.type_.value())
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

    //
    //  Function to align and pad numerics
    //

    void HandleNumericAlignmentAndFill(minstd::string &buffer, const minstd::string &sign, size_t start_of_number, const arg_format_options &format)
    {
        size_t number_length = (buffer.size() - start_of_number) + sign.size();

        //  Add the sign if this is not zero filled

        if (!format.zero_fill_.has_value() || !format.zero_fill_.value())
        {
            buffer += sign;
        }

        //  If the width is specified and the number of characters is less than the width and the
        //      alignment is either right or center, then we need to add fill characters.

        if (format.width_.has_value() &&
            (format.width_.value() > number_length) &&
            ((format.alignment_.value() == arg_format_options::align::right) || (format.alignment_.value() == arg_format_options::align::center)))
        {
            //  Determine how much to fill in front of the number

            size_t fill_count = format.width_.value() - number_length;

            if (!format.zero_fill_.has_value() || !format.zero_fill_.value())
            {
                if (format.alignment_.value() == arg_format_options::align::center)
                {
                    fill_count /= 2;
                }

                for (size_t i = 0; i < fill_count; i++)
                {
                    buffer.push_back(format.fill_.value());
                }
            }
            else
            {
                for (size_t i = 0; i < fill_count; i++)
                {
                    buffer.push_back('0');
                }

                //  Add the sign

                buffer += sign;
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

        number_length = (buffer.size() - start_of_number) + sign.size();

        if (format.width_.has_value() && (format.width_.value() > number_length))
        {
            size_t fill_count = format.width_.value() - number_length;

            for (size_t i = 0; i < fill_count; i++)
            {
                buffer.push_back(format.fill_.value());
            }
        }
    }

    //
    //  Template function to convert floats to string.  Works for all floating point types.
    //

    template <typename T>
    void FloatToString(minstd::string &buffer, const T &arg, const arg_format_options &format)
    {
        static uint64_t const pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

        uint64_t decimal_multiplier = pow10[minstd::min(format.precision_.value(), (uint32_t)8)];

        uint64_t decimals;
        uint64_t units;

        //  Start the conversion

        if (arg < 0)
        {
            //  Negative numbers

            decimals = (uint64_t)(-arg * decimal_multiplier) % decimal_multiplier;
            units = (uint64_t)(-arg);
        }
        else
        {
            //  Positive numbers

            decimals = (uint64_t)(arg * decimal_multiplier) % decimal_multiplier;
            units = (uint64_t)arg;
        }

        size_t start_of_number = buffer.size();

        for (size_t i = 0; i < format.precision_.value(); i++)
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

        //  Get the sign

        minstd::fixed_string<4> sign;

        if (arg < 0)
        {
            sign = "-";
        }

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, sign, start_of_number, format);
    }

    //
    //  Template function to convert unsigned integers to string.  Works for all unsigned integer types.
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

        if (!format.integer_base_.has_value() ||
            ((format.integer_base_.value() < 2) || (format.integer_base_.value() > 36)))
        {
            buffer += "{Invalid base}";
            return;
        }

        //  Get the number in a reversed order

        size_t start_of_number = buffer.size();

        const char *numeric_conversion_digits = (format.type_.has_value() && ((format.type_.value() == 'X') || (format.type_.value() == 'B'))) ? UPPER_CASE_NUMERIC_CONVERSION_DIGITS : LOWER_CASE_NUMERIC_CONVERSION_DIGITS;

        UnsignedIntToReversedString(buffer, value, format.integer_base_.value(), numeric_conversion_digits);

        //  If this is hex, octal or binary and alt is pecified, then we need to add the prefix

        HandleIntegerPrefix(buffer, format);

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

        if (!format.integer_base_.has_value() ||
            ((format.integer_base_.value() < 2) || (format.integer_base_.value() > 36)))
        {
            buffer += "{Invalid base}";
            return;
        }

        //  We need to convert the value to an unsigned type to handle the negative case, only if the base is 10.

        size_t start_of_number = buffer.size();

        const char *numeric_conversion_digits = (format.type_.has_value() && ((format.type_.value() == 'X') || (format.type_.value() == 'B'))) ? UPPER_CASE_NUMERIC_CONVERSION_DIGITS : LOWER_CASE_NUMERIC_CONVERSION_DIGITS;

        typedef typename minstd::make_unsigned<T>::type unsigned_T;

        if (value < 0)
        {
            UnsignedIntToReversedString(buffer, (unsigned_T)-value, format.integer_base_.value(), numeric_conversion_digits);
        }
        else
        {
            UnsignedIntToReversedString(buffer, (unsigned_T)value, format.integer_base_.value(), numeric_conversion_digits);
        }

        //  If this is hex, octal or binary and alt is pecified, then we need to add the prefix

        HandleIntegerPrefix(buffer, format);

        //  Get the sign

        minstd::fixed_string<4> sign;

        if (format.sign_.has_value())
        {
            if (value >= 0)
            {
                if (format.sign_.value() == arg_format_options::sign::always_plus)
                {
                    sign = "+";
                }
                else if (format.sign_.value() == arg_format_options::sign::space)
                {
                    sign = " ";
                }
            }
            else
            {
                sign = "-";
            }
        }
        else if (value < 0)
        {
            sign = "-";
        }

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, sign, start_of_number, format);
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

        if (format_options.width_.has_value() && (format_options.width_.value() > length))
        {
            size_t fill_count = format_options.width_.value() - length;

            if (format_options.alignment_.has_value())
            {
                switch (format_options.alignment_.value())
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
            buffer.push_back(format_options.fill_.value());
        }

        buffer += value;

        for (size_t i = 0; i < fill_after_count; i++)
        {
            buffer.push_back(format_options.fill_.value());
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
    void fmt_arg_base<const void*>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, reinterpret_cast<uint64_t>(value_), format_options);
    }

} // namespace FMT_FORMATTERS_NAMESPACE
