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

    constexpr uint32_t DEFAULT_FLOATING_POINT_PRECISION = 4;

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

    void HandleNumericAlignmentAndFill(minstd::string &buffer, size_t start_of_number, const arg_format_options &format)
    {
        //  If the width is specified and the number of characters is less than the width and the
        //      alignment is either right or center, then we need to add fill characters.

        if (format.width_.has_value() &&
            (format.width_.value() > buffer.size() - start_of_number) &&
            ((format.alignment_.value() == arg_format_options::align::right) || (format.alignment_.value() == arg_format_options::align::center)))
        {
            size_t fill_count = format.width_.value() - (buffer.size() - start_of_number);

            if (format.alignment_.value() == arg_format_options::align::center)
            {
                fill_count /= 2;
            }

            for (size_t i = 0; i < fill_count; i++)
            {
                buffer.push_back(format.fill_.value());
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

        if (format.width_.has_value() && (format.width_.value() > buffer.size() - start_of_number))
        {
            size_t fill_count = format.width_.value() - (buffer.size() - start_of_number);

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

        //  Set the default alignment and fill if they were not specified

        if (!format.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format).alignment_ = arg_format_options::align::right;
        }

        if (!format.fill_.has_value())
        {
            const_cast<arg_format_options &>(format).fill_ = ' ';
        }

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

        if (arg < 0)
        {
            buffer.push_back('-'); //  Add the minus sign
        }

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, start_of_number, format);
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

        //  Set the default alignment and fill if they were not specified

        if (!format.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format).alignment_ = arg_format_options::align::right;
        }

        if (!format.fill_.has_value())
        {
            const_cast<arg_format_options &>(format).fill_ = ' ';
        }

        //  Get the number in a reversed order

        size_t start_of_number = buffer.size();

        const char *numeric_conversion_digits = (format.type_.has_value() && ((format.type_.value() == 'X') || (format.type_.value() == 'B'))) ? UPPER_CASE_NUMERIC_CONVERSION_DIGITS : LOWER_CASE_NUMERIC_CONVERSION_DIGITS;

        UnsignedIntToReversedString(buffer, value, format.integer_base_.value(), numeric_conversion_digits);

        //  If this is hex, octal or binary and alt is pecified, then we need to add the prefix

        HandleIntegerPrefix(buffer, format);

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, start_of_number, format);
    }

    //
    //  Template function to convert signed integers to string.  Works for all signed integer types.
    //

    template <typename T>
    static void SignedIntToString(minstd::string &buffer, T value, const arg_format_options &format)
    {
        //  Insure the requested base is OK

        if (!format.integer_base_.has_value() ||
            ((format.integer_base_.value() < 2) || (format.integer_base_.value() > 36)))
        {
            buffer += "{Invalid base}";
            return;
        }

        //  Set the default alignment and fill if they were not specified

        if (!format.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format).alignment_ = arg_format_options::align::right;
        }

        if (!format.fill_.has_value())
        {
            const_cast<arg_format_options &>(format).fill_ = ' ';
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

        //  Add a negative sign if this is a negative value

        if (value < 0)
        {
            buffer.push_back('-');
        }

        //  Align, pad and unreverse the number

        HandleNumericAlignmentAndFill(buffer, start_of_number, format);
    }

    //
    //  Append methods for the different format argument types
    //

    void CharacterStringFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        if (!format_options.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format_options).alignment_ = arg_format_options::align::left;
        }

        buffer += value_;
    }

    void StringFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        if (!format_options.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format_options).alignment_ = arg_format_options::align::left;
        }

        buffer += value_;
    }

    void Unsigned64BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    void Signed64BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    void Unsigned32BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    void Signed32BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    void Unsigned16BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    void Signed16BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    void Unsigned8BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        UnsignedIntToString(buffer, value_, format_options);
    }

    void Signed8BitIntFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        SignedIntToString(buffer, value_, format_options);
    }

    void FloatFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        if (!format_options.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format_options).alignment_ = arg_format_options::align::right;
        }

        if (!format_options.precision_.has_value())
        {
            const_cast<arg_format_options &>(format_options).precision_ = DEFAULT_FLOATING_POINT_PRECISION;
        }

        FloatToString(buffer, value_, format_options);
    }

    void DoubleFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        if (!format_options.alignment_.has_value())
        {
            const_cast<arg_format_options &>(format_options).alignment_ = arg_format_options::align::right;
        }

        if (!format_options.precision_.has_value())
        {
            const_cast<arg_format_options &>(format_options).precision_ = DEFAULT_FLOATING_POINT_PRECISION;
        }

        FloatToString(buffer, value_, format_options);
    }

    void BoolFormatter::Append(minstd::string &buffer, const arg_format_options &format_options) const
    {
        if(value_)
        {
            buffer += "true";
        }
        else
        {
            buffer += "false";
        }
    }

} // namespace FMT_FORMATTERS_NAMESPACE
