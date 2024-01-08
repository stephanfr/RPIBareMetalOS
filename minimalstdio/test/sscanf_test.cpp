// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "string.h"

#define _ISupper _U
#define _ISlower _L
#define _ISalpha _U | _L
#define _ISdigit _N
#define _ISxdigit _X
#define _ISspace _S
#define _ISprint _U | _L | _N | _S | _P
#define _IScntrl _C
#define _ISpunct _P
#define _ISblank _B

namespace std
{
    using ::memcmp;
};

#include <catch2/catch_test_macros.hpp>

long strtol(const char *numeric_string, char **first_nonconverted_character, int base, int *result_code)
{
    long result = strtol( numeric_string, first_nonconverted_character, base );

    *result_code = errno;

    return result;
}

#include "../include/scanf.h"

namespace
{
    TEST_CASE("sscanf - Empty Buffer and Formats", "")
    {
        char first_string[64];

        REQUIRE(__sscanf("", "%s", first_string) == EOF);
        REQUIRE(__sscanf(NULL, "%s", first_string) == EOF);
        REQUIRE(__sscanf("Test", "", first_string) == 0);
        REQUIRE(__sscanf("Test", NULL, first_string) == EOF);
        REQUIRE(__sscanf("", "") == EOF);
        REQUIRE(__sscanf(NULL, NULL) == EOF);
    }

    TEST_CASE("sscanf - Simple Test", "")
    {
        char test_char;
        char test_char2;
        char test_string[64];
        int decimal_value;
        unsigned decimal_value2;

        REQUIRE(__sscanf("Test", "%s", test_string) == 1);
        REQUIRE(strncmp(test_string, "Test", 64) == 0);

        REQUIRE(__sscanf("12", "%d", &decimal_value) == 1);
        REQUIRE(decimal_value == 12);

        REQUIRE(__sscanf("  TestString   12  ", "%s %d", test_string, &decimal_value) == 2);
        REQUIRE(strncmp(test_string, "TestString", 64) == 0);
        REQUIRE(decimal_value == 12);

        REQUIRE(__sscanf(" A astring", "%c %c %s", &test_char, &test_char2, test_string) == 3);
        REQUIRE( test_char == ' ' );
        REQUIRE( test_char2 == 'A' );
        REQUIRE(strncmp(test_string, "astring", 64) == 0);

        REQUIRE(__sscanf(" -11  -14  ", "%d %u", &decimal_value, &decimal_value2) == 2);
        REQUIRE(decimal_value == -11);
        REQUIRE(decimal_value2 == 4294967282);
    }

    TEST_CASE("sscanf - Hexadecimal Test", "")
    {
        char char_value;
        int hexadecimal_value;
        int decimal_value;

        REQUIRE(__sscanf("A02B", "%x", &hexadecimal_value) == 1);
        REQUIRE(hexadecimal_value == 0xA02B);

        REQUIRE(__sscanf("00E1A02B", "%X", &hexadecimal_value) == 1);
        REQUIRE(hexadecimal_value == 0x00E1A02B);

        REQUIRE(__sscanf("0x12E1A02B", "%X", &hexadecimal_value) == 1);
        REQUIRE(hexadecimal_value == 0x12E1A02B);

        REQUIRE(__sscanf("A02Bq12", "%x%c%d", &hexadecimal_value, &char_value, &decimal_value) == 3);
        REQUIRE(hexadecimal_value == 0xA02B);
        REQUIRE(char_value == 'q');
        REQUIRE(decimal_value == 12);
    }

    TEST_CASE("sscanf - Octal Test", "")
    {
        char char_value;
        int octal_value;
        int decimal_value;

        REQUIRE(__sscanf("1234", "%o", &octal_value) == 1);
        REQUIRE(octal_value == 01234);

        REQUIRE(__sscanf("1234567812", "%o%c%d", &octal_value, &char_value, &decimal_value) == 3);
        REQUIRE(octal_value == 01234567);
        REQUIRE(char_value == '8');
        REQUIRE(decimal_value == 12);
    }

    TEST_CASE("sscanf - i Test", "")
    {
        int octal_value;
        int decimal_value;
        int hexadecimal_value;

        REQUIRE(__sscanf("1234 0234 0x89ab", "%i %i %i", &decimal_value, &octal_value, &hexadecimal_value) == 3);
        REQUIRE(decimal_value == 1234);
        REQUIRE(octal_value == 0234);
        REQUIRE(hexadecimal_value == 0x89AB);
    }

    TEST_CASE("sscanf - Long Argument Test", "")
    {
        long decimal_value;
        long hexadecimal_value;
        long octal_value;

        REQUIRE(__sscanf("123", "%ld", &decimal_value) == 1);
        REQUIRE(decimal_value == 123);

        REQUIRE(__sscanf("456 0x12E1A02B 1234", "%ld%lX%lo", &decimal_value, &hexadecimal_value, &octal_value) == 3);
        REQUIRE(decimal_value == 456);
        REQUIRE(hexadecimal_value == 0x12E1A02B);
        REQUIRE(octal_value == 01234);
    }

    TEST_CASE("sscanf - Suppress Assignment Test", "")
    {
        char test_string[64];
        int decimal_value;

        REQUIRE(__sscanf("Test", "%s", test_string) == 1);
        REQUIRE(strncmp(test_string, "Test", 64) == 0);

        REQUIRE(__sscanf("12", "%d", &decimal_value) == 1);
        REQUIRE(decimal_value == 12);

        REQUIRE(__sscanf("  one  two   12   13  ", "%*s %s %*d %d", test_string, &decimal_value) == 2);
        REQUIRE(strncmp(test_string, "two", 64) == 0);
        REQUIRE(decimal_value == 13);
    }

    TEST_CASE("sscanf - Test Max Field Length", "")
    {
        char test_string[64];
        int decimal_value1;
        int decimal_value2;

        REQUIRE(__sscanf(" abcdefghijklmnopqrstuvwxyz 1234567 ", "%10s %*s %4d %d", test_string, &decimal_value1, &decimal_value2) == 3);
        REQUIRE(strncmp(test_string, "abcdefghij", 64) == 0);
        REQUIRE(decimal_value1 == 1234);
        REQUIRE(decimal_value2 == 567);
    }

    TEST_CASE("sscanf - Test Match Set", "")
    {
        char test_string[64];
        char test_string2[64];
        int decimal_value;

        REQUIRE(__sscanf("twghiijkuv 1234 ", "%[tuvwghijk] %d", test_string, &decimal_value) == 2);
        REQUIRE(strncmp(test_string, "twghiijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        REQUIRE(__sscanf("   twghiaijkuv 1234 ", " %[tuvwghijk] %s %d", test_string, test_string2, &decimal_value) == 3);
        REQUIRE(strncmp(test_string, "twghi", 64) == 0);
        REQUIRE(strncmp(test_string2, "aijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        REQUIRE(__sscanf(" twghicijkuv 1234 ", " %[^abcdef] %s %d", test_string, test_string2, &decimal_value) == 3);
        REQUIRE(strncmp(test_string, "twghi", 64) == 0);
        REQUIRE(strncmp(test_string2, "cijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        //  Test with closing square bracket

        REQUIRE(__sscanf(" twghi]aijkuv 1234 ", " %[]tuvwghijk] %s %d", test_string, test_string2, &decimal_value) == 3);
        REQUIRE(strncmp(test_string, "twghi]", 64) == 0);
        REQUIRE(strncmp(test_string2, "aijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        REQUIRE(__sscanf(" twghi]ijkuv 1234 ", " %[^]abcdef] %s %d", test_string, test_string2, &decimal_value) == 3);
        REQUIRE(strncmp(test_string, "twghi", 64) == 0);
        REQUIRE(strncmp(test_string2, "]ijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        //  Test with space

        REQUIRE(__sscanf(" twgh iaijkuv 1234 ", " %[tuvwghij k] %s %d", test_string, test_string2, &decimal_value) == 3);
        REQUIRE(strncmp(test_string, "twgh i", 64) == 0);
        REQUIRE(strncmp(test_string2, "aijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        REQUIRE(__sscanf(" twghi ijkuv 1234 ", " %[^abcde f] %s %d", test_string, test_string2, &decimal_value) == 3);
        REQUIRE(strncmp(test_string, "twghi", 64) == 0);
        REQUIRE(strncmp(test_string2, "ijkuv", 64) == 0);
        REQUIRE(decimal_value == 1234);

        //  Error conditions

        REQUIRE(__sscanf("twghiijkuv 1234 ", "%[", test_string, &decimal_value) == EOF);
        REQUIRE(__sscanf("twghiijkuv 1234 ", "%[]", test_string, &decimal_value) == EOF);
        REQUIRE(__sscanf("twghiijkuv 1234 ", "%[] %d", test_string, &decimal_value) == EOF);
        REQUIRE(__sscanf("twghiijkuv 1234 ", "%[tuvwghijk %d", test_string, &decimal_value) == EOF);
    }
    
    
    TEST_CASE("sscanf - Literals in Format Test", "")
    {
        char test_string[64];
        int decimal_value;

        REQUIRE(__sscanf("  Test aftertest", " Test%s", test_string) == 1);
        REQUIRE(strncmp(test_string, "aftertest", 64) == 0);

        REQUIRE(__sscanf("  Test aftertest2", " Test  %s", test_string) == 1);
        REQUIRE(strncmp(test_string, "aftertest2", 64) == 0);

        REQUIRE(__sscanf("  Test Test2 aftertest3", "   Test   Test2   %s", test_string) == 1);
        REQUIRE(strncmp(test_string, "aftertest3", 64) == 0);

        REQUIRE(__sscanf("  Test    Test2    aftertest4", " Test Test2 %s", test_string) == 1);
        REQUIRE(strncmp(test_string, "aftertest4", 64) == 0);

        REQUIRE(__sscanf("abc12defanotherstring 'this should be skipped'", "abc%ddef%s", &decimal_value, test_string) == 2);
        REQUIRE(decimal_value == 12);
        REQUIRE(strncmp(test_string, "anotherstring", 64) == 0);

        REQUIRE(__sscanf("abc 12 defanotherstring 'this should be skipped'", "abc %d def%s", &decimal_value, test_string) == 2);
        REQUIRE(decimal_value == 12);
        REQUIRE(strncmp(test_string, "anotherstring", 64) == 0);

        REQUIRE(__sscanf("  Test aftertest", "Test %s", test_string) == 0);
    }

}