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

        STRCMP_EQUAL("", minstd::format(formatted_string, "").c_str());
        STRCMP_EQUAL("This is a test: \n", minstd::format(formatted_string, "This is a test: \n").c_str());

        //  Case of no closing brace

        STRCMP_EQUAL("This is a test: ", minstd::format(formatted_string, "This is a test: {\n", (uint32_t)423).c_str());

        //  Format string containing only a colon

        STRCMP_EQUAL("This is a test: 423\n", minstd::format(formatted_string, "This is a test: {:}\n", (uint32_t)423).c_str());
        
        //  Alignment without width
        
        STRCMP_EQUAL("This is a test: of alignment   \n", minstd::format(formatted_string, "This is a test: {:<} {:12}\n", "of", "alignment").c_str());

        //  Alt has no effect on integers without a type specifier

        STRCMP_EQUAL("No Effect: 48", minstd::format(formatted_string, "No Effect: {:#}", (int16_t)48).c_str());

        //  Zero fill has not effect without a width and is ignored when an alignment character is supplied

        STRCMP_EQUAL("This is a test: 423 of no effect zero prefixing", minstd::format(formatted_string, "This is a test: {:0} {}", 423, "of no effect zero prefixing").c_str());
        STRCMP_EQUAL("This is a test:    423 of no effect zero prefixing", minstd::format(formatted_string, "This is a test: {:>06} {}", 423, "of no effect zero prefixing").c_str());

        //  Menaingless type specifier

        STRCMP_EQUAL("Meaningless type: 47", minstd::format(formatted_string, "Meaningless type: {:q}", 47).c_str());
    }

    TEST(FormatTests, Chars)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: of format()", minstd::format(formatted_string, "This is a test: {}{} format()", 'o', 'f').c_str());
    }

    TEST(FormatTests, CharStrings)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: of format()", minstd::format(formatted_string, "This is a test: {} {}", "of", "format()").c_str());
    }

    TEST(FormatTests, STDStrings)
    {
        minstd::fixed_string<256> formatted_string;

        const minstd::fixed_string<64> arg1("that");

        STRCMP_EQUAL("This is a test: that worked\n", minstd::format(formatted_string, "This is a test: {} {}\n", arg1, minstd::fixed_string<64>("worked")));
    }

    TEST(FormatTests, 8BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -123", minstd::format(formatted_string, "This is a test: {}", (int8_t)-123).c_str());
        STRCMP_EQUAL("This is a test: 255\n", minstd::format(formatted_string, "This is a test: {}\n", (uint8_t)255).c_str());
    }

    TEST(FormatTests, 16BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -32000", minstd::format(formatted_string, "This is a test: {}", (int16_t)-32000).c_str());
        STRCMP_EQUAL("This is a test: 65535\n", minstd::format(formatted_string, "This is a test: {}\n", (uint16_t)65535).c_str());
    }

    TEST(FormatTests, 32BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -123", minstd::format(formatted_string, "This is a test: {}", (int32_t)-123).c_str());
        STRCMP_EQUAL("This is a test: 423\n", minstd::format(formatted_string, "This is a test: {}\n", (uint32_t)423).c_str());
    }

    TEST(FormatTests, 64BitSignedAndUnsignedInts)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: -123", minstd::format(formatted_string, "This is a test: {}", (int64_t)-123).c_str());
        STRCMP_EQUAL("This is a test: 423\n", minstd::format(formatted_string, "This is a test: {}\n", (uint64_t)423).c_str());
    }

    TEST(FormatTests, Bools)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: true false\n", minstd::format(formatted_string, "This is a test: {} {}\n", true, false).c_str());
    }

    TEST(FormatTests, Floats)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 12.3400 -56.7800\n", minstd::format(formatted_string, "This is a test: {} {}\n", (float)12.34, (float)-56.78).c_str());
        STRCMP_EQUAL("This is a test: 12.0000 -56.0000\n", minstd::format(formatted_string, "This is a test: {} {}\n", (float)12, (float)-56).c_str());
    }

    TEST(FormatTests, Doubles)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 12.3400 -56.7800\n", minstd::format(formatted_string, "This is a test: {} {}\n", (double)12.34, (double)-56.78).c_str());
    }

    TEST(FormatTests, Pointer)
    {
        minstd::fixed_string<256> formatted_string;

        STRNCMP_EQUAL("This is a test: 0X", minstd::format(formatted_string, "This is a test: {}\n", &formatted_string).c_str(), 18);
        STRNCMP_EQUAL("This is a test: 0X", minstd::format(formatted_string, "This is a test: {:p}\n", &formatted_string).c_str(), 18);
    }

    TEST(FormatTests, PositionalArguments)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 6789 423 6789 6789\n", minstd::format(formatted_string, "This is a test: {1} {0} {1} {1}\n", (uint32_t)423, (uint32_t)6789, "Should not be this argument").c_str());
    }

    TEST(FormatTests, AlignAndFill)
    {
        minstd::fixed_string<256> formatted_string;

        //  String alignment and fill

        STRCMP_EQUAL("This is a test: of     alignment   \n", minstd::format(formatted_string, "This is a test: {:<6} {:12}\n", "of", "alignment").c_str());
        STRCMP_EQUAL("This is a test:     of alignment   \n", minstd::format(formatted_string, "This is a test: {:>6} {:<12}\n", "of", "alignment").c_str());
        STRCMP_EQUAL("This is a test:   of   -alignment--\n", minstd::format(formatted_string, "This is a test: {:^6} {:-^12}\n", "of", "alignment").c_str());
        STRCMP_EQUAL("This is a test: **of** alignment\n", minstd::format(formatted_string, "This is a test: {:*^6} {:-^6}\n", "of", "alignment").c_str());

        //  Integer alignment and fill

        STRCMP_EQUAL("This is a test: 423***    6789\n", minstd::format(formatted_string, "This is a test: {:*<6} {:7}\n", (uint32_t)423, (int32_t)6789).c_str());
        STRCMP_EQUAL("This is a test: *423**  6789\n", minstd::format(formatted_string, "This is a test: {:*^6} {:>5}\n", (uint32_t)423, (int32_t)6789).c_str());
        STRCMP_EQUAL("This is a test: 7654     \n", minstd::format(formatted_string, "This is a test: {:<9}\n", (uint32_t)7654).c_str());
        STRCMP_EQUAL("This is a test: 50000 00600 00007", minstd::format(formatted_string, "This is a test: {:0<5} {:0^5} {:0>5}", 5, 6, 7).c_str());
    }

    TEST(FormatTests, Sign)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: +423 of sign specification", minstd::format(formatted_string, "This is a test: {:+} {}", 423, "of sign specification").c_str());
        STRCMP_EQUAL("This is a test:  423 of sign specification", minstd::format(formatted_string, "This is a test: {: } {}", 423, "of sign specification").c_str());
        STRCMP_EQUAL("This is a test: 423 of sign specification", minstd::format(formatted_string, "This is a test: {:-} {}", 423, "of sign specification").c_str());

        STRCMP_EQUAL("This is a test:   +423 of sign specification", minstd::format(formatted_string, "This is a test: {:+6} {}", 423, "of sign specification").c_str());
        STRCMP_EQUAL("This is a test:    423 of sign specification", minstd::format(formatted_string, "This is a test: {: 6} {}", 423, "of sign specification").c_str());
        STRCMP_EQUAL("This is a test:    423 of sign specification", minstd::format(formatted_string, "This is a test: {:-6} {}", 423, "of sign specification").c_str());

        STRCMP_EQUAL("This is a test: +00423 of sign specification", minstd::format(formatted_string, "This is a test: {:+06d} {}", 423, "of sign specification").c_str());
    }

    TEST(FormatTests, ZeroPrefix)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("This is a test: 000423 of zero prefixing", minstd::format(formatted_string, "This is a test: {:06} {}", 423, "of zero prefixing").c_str());
    }

    TEST(FormatTests, Precision)
    {
        minstd::fixed_string<256> formatted_string;

        STRCMP_EQUAL("Floating Point Precision Test: 12.3400 -56.78000\n", minstd::format(formatted_string, "Floating Point Precision Test: {:.4} {:.5}\n", (float)12.34, (double)-56.78).c_str());
        STRCMP_EQUAL("Floating Point Formatting Test: |12.3400  | |  -56.78000|\n", minstd::format(formatted_string, "Floating Point Formatting Test: |{:<9.4}| |{:>11.5}|\n", (float)12.34, (double)-56.78).c_str());
    }

    TEST(FormatTests, IntegerFormatting)
    {
        minstd::fixed_string<256> formatted_string;

        //  Hexadecimal formatting

        STRCMP_EQUAL("Hex Format: 2f", minstd::format(formatted_string, "Hex Format: {:x}", (int16_t)47).c_str());
        STRCMP_EQUAL("Hex Format: 0x30", minstd::format(formatted_string, "Hex Format: {:#x}", (int16_t)48).c_str());
        STRCMP_EQUAL("Hex Format: 2F", minstd::format(formatted_string, "Hex Format: {:X}", (uint16_t)47).c_str());
        STRCMP_EQUAL("Hex Format: 0X2E", minstd::format(formatted_string, "Hex Format: {:#X}", (uint16_t)46).c_str());

        STRCMP_EQUAL("Hex Format: +0X2E", minstd::format(formatted_string, "Hex Format: {:+#X}", (int16_t)46).c_str());
        STRCMP_EQUAL("Hex Format:  0X2E", minstd::format(formatted_string, "Hex Format: {: #X}", (int16_t)46).c_str());
        STRCMP_EQUAL("Hex Format: 0X2E", minstd::format(formatted_string, "Hex Format: {:-#X}", (int16_t)46).c_str());
        STRCMP_EQUAL("Hex Format: -2f", minstd::format(formatted_string, "Hex Format: {:x}", (int16_t)-47).c_str());
        STRCMP_EQUAL("Hex Format: -0x30", minstd::format(formatted_string, "Hex Format: {:#x}", (int16_t)-48).c_str());
        STRCMP_EQUAL("Hex Format: -0x30", minstd::format(formatted_string, "Hex Format: {:-#x}", (int16_t)-48).c_str());

        //  Binary formatting

        STRCMP_EQUAL("Binary Format: 101111", minstd::format(formatted_string, "Binary Format: {:b}", (int16_t)47).c_str());
        STRCMP_EQUAL("Binary Format: 0b110000", minstd::format(formatted_string, "Binary Format: {:#b}", (int16_t)48).c_str());
        STRCMP_EQUAL("Binary Format: 101111", minstd::format(formatted_string, "Binary Format: {:B}", (uint16_t)47).c_str());
        STRCMP_EQUAL("Binary Format: 0B101110", minstd::format(formatted_string, "Binary Format: {:#B}", (uint16_t)46).c_str());

        STRCMP_EQUAL("Binary Format: -101111", minstd::format(formatted_string, "Binary Format: {:b}", (int16_t)-47).c_str());
        STRCMP_EQUAL("Binary Format: -0b110000", minstd::format(formatted_string, "Binary Format: {:#b}", (int16_t)-48).c_str());

        //  Octal formatting

        STRCMP_EQUAL("Octal Format: 57", minstd::format(formatted_string, "Octal Format: {:o}", (int16_t)47).c_str());
        STRCMP_EQUAL("Octal Format: 060", minstd::format(formatted_string, "Octal Format: {:#o}", (uint16_t)48).c_str());

        STRCMP_EQUAL("Octal Format: -57", minstd::format(formatted_string, "Octal Format: {:o}", (int16_t)-47).c_str());
        STRCMP_EQUAL("Octal Format: -060", minstd::format(formatted_string, "Octal Format: {:#o}", (int16_t)-48).c_str());
    }

    TEST(FormatTests, Errors)
    {
        minstd::fixed_string<256> formatted_string;

        //  Format string with an invalid positional argument

        STRCMP_EQUAL("This is a test: {Invalid format string: '.5'}\n", minstd::format(formatted_string, "This is a test: {.5}\n", (uint32_t)423).c_str());

        //  Argument Id out of range

        STRCMP_EQUAL("This is a test: {Argument position out of range} of            \n", minstd::format(formatted_string, "This is a test: {2:<6} {:14}\n", "of", "bad alignment").c_str());

        //  Two fill characters before alignment symbol or fill of '{' or '}'

        STRCMP_EQUAL("This is a test: {Invalid format string: ':**<6'} bad alignment \n", minstd::format(formatted_string, "This is a test: {:**<6} {:14}\n", "of", "bad alignment").c_str());
        STRCMP_EQUAL("This is a test: {Invalid format string: ':{<6'} bad alignment \n", minstd::format(formatted_string, "This is a test: {:{<6} {:14}\n", "of", "bad alignment").c_str());
        STRCMP_EQUAL("This is a test: {Invalid format string: ':}<'}6} of            \n", minstd::format(formatted_string, "This is a test: {:}<6} {:14}\n", "of", "bad alignment").c_str());
    }

} // namespace
