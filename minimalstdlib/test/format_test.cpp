// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <fixed_string>
#include <format>
#include <format_formatters>

#include <ctype.h>

static unsigned __seven_bit_ascii_ctype_table[] = {
    _C, _C, _C, _C, _C, _C, _C, _C,
    _C, _C | _S | _B, _C | _S, _C | _S, _C | _S, _C | _S, _C, _C,
    _C, _C, _C, _C, _C, _C, _C, _C,
    _C, _C, _C, _C, _C, _C, _C, _C,
    _S | _B, _P, _P, _P, _P, _P, _P, _P,
    _P, _P, _P, _P, _P, _P, _P, _P,
    _N | _X, _N | _X, _N | _X, _N | _X, _N | _X, _N | _X, _N | _X, _N | _X,
    _N | _X, _N | _X, _P, _P, _P, _P, _P, _P,
    _P, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U,
    _U, _U, _U, _U, _U, _U, _U, _U,
    _U, _U, _U, _U, _U, _U, _U, _U,
    _U, _U, _U, _P, _P, _P, _P, _P,
    _P, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L,
    _L, _L, _L, _L, _L, _L, _L, _L,
    _L, _L, _L, _L, _L, _L, _L, _L,
    _L, _L, _L, _P, _P, _P, _P, _C};

extern unsigned *__get_ctype_table()
{
    return __seven_bit_ascii_ctype_table;
}

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FormatTests)
    {
    };
#pragma GCC diagnostic pop

    TEST(FormatTests, EdgeCases)
    {
        minstd::fixed_string<256> formatted_string;

        //  Empty format string and no arguments

        STRCMP_EQUAL("", minstd::fmt::format(formatted_string, "").c_str());
        STRCMP_EQUAL("This is a test: \n", minstd::fmt::format(formatted_string, "This is a test: \n").c_str());

        //  Case of no closing brace

        STRCMP_EQUAL("This is a test: ", minstd::fmt::format(formatted_string, "This is a test: {\n", (uint32_t)423).c_str());

        //  Format string containing only a colon

        STRCMP_EQUAL("This is a test: 423\n", minstd::fmt::format(formatted_string, "This is a test: {:}\n", (uint32_t)423).c_str());

        //  Format string with an invalid positional argument

        STRCMP_EQUAL("This is a test: {Invalid format string: '.5'}\n", minstd::fmt::format(formatted_string, "This is a test: {.5}\n", (uint32_t)423).c_str());
    }

    TEST(FormatTests, CharStrings)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: of fmt::format()", minstd::fmt::format(formatted_string, "This is a test: {} {}", "of", "fmt::format()").c_str());
    }

    TEST(FormatTests, STDStrings)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: that worked\n", minstd::fmt::format(formatted_string, "This is a test: {} {}\n", minstd::fixed_string<64>("that"), minstd::fixed_string<64>("worked")));
    }

    TEST(FormatTests, 8BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -123", minstd::fmt::format(formatted_string, "This is a test: {}", (int8_t)-123).c_str());
        STRCMP_EQUAL("This is a test: 255\n", minstd::fmt::format(formatted_string, "This is a test: {}\n", (uint8_t)255).c_str());
    }

    TEST(FormatTests, 16BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -32000", minstd::fmt::format(formatted_string, "This is a test: {}", (int16_t)-32000).c_str());
        STRCMP_EQUAL("This is a test: 65535\n", minstd::fmt::format(formatted_string, "This is a test: {}\n", (uint16_t)65535).c_str());
    }

    TEST(FormatTests, 32BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -123", minstd::fmt::format(formatted_string, "This is a test: {}", (int32_t)-123).c_str());
        STRCMP_EQUAL("This is a test: 423\n", minstd::fmt::format(formatted_string, "This is a test: {}\n", (uint32_t)423).c_str());
    }

    TEST(FormatTests, 64BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -123", minstd::fmt::format(formatted_string, "This is a test: {}", (int64_t)-123).c_str());
        STRCMP_EQUAL("This is a test: 423\n", minstd::fmt::format(formatted_string, "This is a test: {}\n", (uint64_t)423).c_str());
    }

    TEST(FormatTests, Floats)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 12.3400 -56.7800\n", minstd::fmt::format(formatted_string, "This is a test: {} {}\n", (float)12.34, (float)-56.78).c_str());
    }

    TEST(FormatTests, Doubles)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 12.3400 -56.7800\n", minstd::fmt::format(formatted_string, "This is a test: {} {}\n", (double)12.34, (double)-56.78).c_str());
    }

    TEST(FormatTests, PositionalArguments)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 6789 423 6789 6789\n", minstd::fmt::format(formatted_string, "This is a test: {1} {0} {1} {1}\n", (uint32_t)423, (uint32_t)6789, "Should not be this argument").c_str());
    }

    TEST(FormatTests, AlignAndFill)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 423***    6789\n", minstd::fmt::format(formatted_string, "This is a test: {:*<6} {:7}\n", (uint32_t)423, (int32_t)6789).c_str());
        STRCMP_EQUAL("This is a test: *423**  6789\n", minstd::fmt::format(formatted_string, "This is a test: {:*^6} {:>5}\n", (uint32_t)423, (int32_t)6789).c_str());
        STRCMP_EQUAL("This is a test: 7654     \n", minstd::fmt::format(formatted_string, "This is a test: {:<9}\n", (uint32_t)7654).c_str());
        STRCMP_EQUAL("This is a test: 50000 00600 00007", minstd::fmt::format(formatted_string, "This is a test: {:0<5} {:0^5} {:0>5}", 5, 6, 7).c_str());
    }

    TEST(FormatTests, ZeroPrefix)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 000423 of zero prefixing", minstd::fmt::format(formatted_string, "This is a test: {:06} {}", 423, "of zero prefixing").c_str());
    }

    TEST(FormatTests, Precision)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("Floating Point Precision Test: 12.3400 -56.78000\n", minstd::fmt::format(formatted_string, "Floating Point Precision Test: {:.4} {:.5}\n", (float)12.34, (double)-56.78).c_str());
        STRCMP_EQUAL("Floating Point Formatting Test: |12.3400  | |  -56.78000|\n", minstd::fmt::format(formatted_string, "Floating Point Formatting Test: |{:<9.4}| |{:>11.5}|\n", (float)12.34, (double)-56.78).c_str());
    }

    TEST(FormatTests, IntegerFormatting)
    {
        minstd::fixed_string<256> formatted_string;

        //  Hexadecimal formatting

        STRCMP_EQUAL("Hex Format: 2f", minstd::fmt::format(formatted_string, "Hex Format: {:x}", (int16_t)47).c_str());
        STRCMP_EQUAL("Hex Format: 0x30", minstd::fmt::format(formatted_string, "Hex Format: {:#x}", (int16_t)48).c_str());
        STRCMP_EQUAL("Hex Format: 2F", minstd::fmt::format(formatted_string, "Hex Format: {:X}", (uint16_t)47).c_str());
        STRCMP_EQUAL("Hex Format: 0X2E", minstd::fmt::format(formatted_string, "Hex Format: {:#X}", (uint16_t)46).c_str());

        STRCMP_EQUAL("Hex Format: -2f", minstd::fmt::format(formatted_string, "Hex Format: {:x}", (int16_t)-47).c_str());
        STRCMP_EQUAL("Hex Format: -0x30", minstd::fmt::format(formatted_string, "Hex Format: {:#x}", (int16_t)-48).c_str());

        //  Binary formatting

        STRCMP_EQUAL("Binary Format: 101111", minstd::fmt::format(formatted_string, "Binary Format: {:b}", (int16_t)47).c_str());
        STRCMP_EQUAL("Binary Format: 0b110000", minstd::fmt::format(formatted_string, "Binary Format: {:#b}", (int16_t)48).c_str());
        STRCMP_EQUAL("Binary Format: 101111", minstd::fmt::format(formatted_string, "Binary Format: {:B}", (uint16_t)47).c_str());
        STRCMP_EQUAL("Binary Format: 0B101110", minstd::fmt::format(formatted_string, "Binary Format: {:#B}", (uint16_t)46).c_str());

        STRCMP_EQUAL("Binary Format: -101111", minstd::fmt::format(formatted_string, "Binary Format: {:b}", (int16_t)-47).c_str());
        STRCMP_EQUAL("Binary Format: -0b110000", minstd::fmt::format(formatted_string, "Binary Format: {:#b}", (int16_t)-48).c_str());

        //  Octal formatting

        STRCMP_EQUAL("Octal Format: 57", minstd::fmt::format(formatted_string, "Octal Format: {:o}", (int16_t)47).c_str());
        STRCMP_EQUAL("Octal Format: 060", minstd::fmt::format(formatted_string, "Octal Format: {:#o}", (uint16_t)48).c_str());

        STRCMP_EQUAL("Octal Format: -57", minstd::fmt::format(formatted_string, "Octal Format: {:o}", (int16_t)-47).c_str());
        STRCMP_EQUAL("Octal Format: -060", minstd::fmt::format(formatted_string, "Octal Format: {:#o}", (int16_t)-48).c_str());
    }

} // namespace
