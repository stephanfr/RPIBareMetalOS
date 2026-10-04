// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "utility/hex_parsers.h"


namespace
{
// ============================================================================
//  HexDigitValue Tests
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HexDigitValueTests)
    {};
#pragma GCC diagnostic pop

    TEST(HexDigitValueTests, NumericDigitsMapToTheirValue)
    {
        for (char c = '0'; c <= '9'; ++c)
        {
            CHECK_EQUAL(c - '0', HexDigitValue(c));
        }
    }

    TEST(HexDigitValueTests, LowercaseLettersMapToTenThroughFifteen)
    {
        for (char c = 'a'; c <= 'f'; ++c)
        {
            CHECK_EQUAL(c - 'a' + 10, HexDigitValue(c));
        }
    }

    TEST(HexDigitValueTests, UppercaseLettersMapToTenThroughFifteen)
    {
        for (char c = 'A'; c <= 'F'; ++c)
        {
            CHECK_EQUAL(c - 'A' + 10, HexDigitValue(c));
        }
    }

    TEST(HexDigitValueTests, NonHexCharactersReturnNegativeOne)
    {
        //  Non-hex letters, punctuation, whitespace, null, and common separators
        const char invalid[] = {
            'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
            'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
            'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
            'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
            ' ', '\0', '$', '_', '+', '-', '.', ',', ';', ':',
            '/', '\\', '|', '(', ')', '[', ']', '{', '}',
            '<', '>', '!', '@', '#', '%', '^', '&', '*', '~',
            '`', '=', '\'', '"'
        };
        for (size_t i = 0; i < sizeof(invalid); ++i)
        {
            CHECK_EQUAL(-1, HexDigitValue(invalid[i]));
        }
    }

    // ============================================================================
    //  ParseHexUint32 - Basic Values
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ParseHexUint32BasicTests)
    {};
#pragma GCC diagnostic pop

    TEST(ParseHexUint32BasicTests, EmptyAndZeroStrings)
    {
        CHECK_EQUAL(0, ParseHexUint32(""));
        CHECK_EQUAL(0, ParseHexUint32("0"));
        CHECK_EQUAL(0, ParseHexUint32("00000000"));
    }

    TEST(ParseHexUint32BasicTests, SingleDigitValues)
    {
        CHECK_EQUAL(9, ParseHexUint32("9"));
        CHECK_EQUAL(10, ParseHexUint32("a"));
        CHECK_EQUAL(15, ParseHexUint32("F"));
    }

    TEST(ParseHexUint32BasicTests, MultiDigitValues)
    {
        CHECK_EQUAL(0x1A, ParseHexUint32("1A"));
        CHECK_EQUAL(0xFF, ParseHexUint32("FF"));
        CHECK_EQUAL(0xFFFFFFFF, ParseHexUint32("FFFFFFFF"));
        CHECK_EQUAL(0xFFFFFFFE, ParseHexUint32("FFFFFFFE"));
    }

    // ============================================================================
    //  ParseHexUint32 - 0x / 0X Prefix Handling
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ParseHexUint32PrefixTests)
    {};
#pragma GCC diagnostic pop

    TEST(ParseHexUint32PrefixTests, LowercasePrefixStripped)
    {
        CHECK_EQUAL(0x10, ParseHexUint32("0x10"));
        CHECK_EQUAL(0xFF, ParseHexUint32("0xFF"));
        CHECK_EQUAL(0xFFFFFFFF, ParseHexUint32("0xFFFFFFFF"));
        CHECK_EQUAL(0xA, ParseHexUint32("0xA"));
    }

    TEST(ParseHexUint32PrefixTests, UppercasePrefixStripped)
    {
        CHECK_EQUAL(0x10, ParseHexUint32("0X10"));
        CHECK_EQUAL(0xFF, ParseHexUint32("0XFF"));
        CHECK_EQUAL(0xFFFFFFFF, ParseHexUint32("0XFFFFFFFF"));
    }

    TEST(ParseHexUint32PrefixTests, MixedCaseWithPrefix)
    {
        CHECK_EQUAL(0xAbCd, ParseHexUint32("0xAbCd"));
        CHECK_EQUAL(0xAbCdEf01, ParseHexUint32("0xAbCdEf01"));
    }

    TEST(ParseHexUint32PrefixTests, PrefixOnlyWithNoDigitsReturnsZero)
    {
        CHECK_EQUAL(0, ParseHexUint32("0x"));
        CHECK_EQUAL(0, ParseHexUint32("0X"));
    }

    // ============================================================================
    //  ParseHexUint32 - Stop on Invalid Character
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ParseHexUint32StopTests)
    {};
#pragma GCC diagnostic pop

    TEST(ParseHexUint32StopTests, StopsAtWhitespaceAndControlChars)
    {
        CHECK_EQUAL(0x1A, ParseHexUint32("1A BC"));
        CHECK_EQUAL(0x1A, ParseHexUint32("1A\tBC"));
        CHECK_EQUAL(0x1A, ParseHexUint32("1A\nBC"));
        CHECK_EQUAL(0x1A, ParseHexUint32("1A\rBC"));
        CHECK_EQUAL(0x1A, ParseHexUint32("1A\0BC"));
    }

    TEST(ParseHexUint32StopTests, StopsAtNonHexSymbols)
    {
        CHECK_EQUAL(0x1F, ParseHexUint32("1F$20"));
        CHECK_EQUAL(0xAB, ParseHexUint32("AB_CD"));
        CHECK_EQUAL(0xABCDEF, ParseHexUint32("ABCdefg123"));
    }

    TEST(ParseHexUint32StopTests, StopsAfterPrefixWhenNextCharInvalid)
    {
        CHECK_EQUAL(0x1, ParseHexUint32("0x1g2"));
        CHECK_EQUAL(0, ParseHexUint32("0xg1"));
        CHECK_EQUAL(0, ParseHexUint32("0x 1A"));
    }

    // ============================================================================
    //  ParseHexUint32 - Edge Cases
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ParseHexUint32EdgeCasesTests)
    {};
#pragma GCC diagnostic pop

    TEST(ParseHexUint32EdgeCasesTests, LeadingZerosAreIgnored)
    {
        CHECK_EQUAL(0x1A, ParseHexUint32("00001A"));
        CHECK_EQUAL(0x1A, ParseHexUint32("0x00001A"));
    }

    TEST(ParseHexUint32EdgeCasesTests, OverEightDigitsWrapModuloTwoToTheThirtyTwo)
    {
        //  0x123456789ABC mod 2^32 == 0x56789ABC
        CHECK_EQUAL(0x56789ABC, ParseHexUint32("123456789ABC"));
        CHECK_EQUAL(0x56789ABC, ParseHexUint32("0x123456789ABC"));
        //  0x100000000 mod 2^32 == 0
        CHECK_EQUAL(0, ParseHexUint32("100000000"));
        //  0xFFFFFFFFF mod 2^32 == 0xFFFFFFFF
        CHECK_EQUAL(0xFFFFFFFF, ParseHexUint32("FFFFFFFFF"));
    }

    TEST(ParseHexUint32EdgeCasesTests, MixedCaseDigitsParseCorrectly)
    {
        CHECK_EQUAL(0xAbCdEf01, ParseHexUint32("AbCdEf01"));
        CHECK_EQUAL(0xAbCdEf01, ParseHexUint32("0xAbCdEf01"));
    }

    TEST(ParseHexUint32EdgeCasesTests, SignAndWhitespaceOnlyReturnZero)
    {
        CHECK_EQUAL(0, ParseHexUint32("-1A"));
        CHECK_EQUAL(0, ParseHexUint32("+1A"));
        CHECK_EQUAL(0, ParseHexUint32("   "));
    }

    // ============================================================================
    //  ParseHexUint32 - Real-World Usage Patterns
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HexParsersIntegrationTests)
    {};
#pragma GCC diagnostic pop

    TEST(HexParsersIntegrationTests, ParsesTypicalValues)
    {
        CHECK_EQUAL(0x1000, ParseHexUint32("0x1000"));       // memory address
        CHECK_EQUAL(0xFF00AA, ParseHexUint32("0xFF00AA"));   // color
        CHECK_EQUAL(0xDEADBEEF, ParseHexUint32("0xDEADBEEF"));// identifier
        CHECK_EQUAL(0xC0FFEE, ParseHexUint32("C0FFEE"));     // no prefix
    }

    TEST(HexParsersIntegrationTests, PartialStringParsesLeadingHex)
    {
        CHECK_EQUAL(0x1A, ParseHexUint32("0x1A rest of string"));
    }

    // ============================================================================
    //  ParseHexUint32 - Determinism
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ParseHexUint32DeterminismTests)
    {};
#pragma GCC diagnostic pop

    TEST(ParseHexUint32DeterminismTests, SameInputAlwaysSameOutput)
    {
        CHECK_EQUAL(ParseHexUint32("0xDEADBEEF"), ParseHexUint32("0xDEADBEEF"));
    }

    TEST(ParseHexUint32DeterminismTests, DifferentInputsProduceDifferentValues)
    {
        CHECK(ParseHexUint32("1A") != ParseHexUint32("1B"));
    }

    TEST(ParseHexUint32DeterminismTests, PrefixAndBareFormAgree)
    {
        CHECK_EQUAL(ParseHexUint32("C0FFEE"), ParseHexUint32("0xC0FFEE"));
    }
}
