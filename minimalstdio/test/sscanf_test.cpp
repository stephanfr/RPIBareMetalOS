// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <string.h>

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

#include "../include/scanf.h"

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ScanfTests)
    {};
#pragma GCC diagnostic pop

    TEST(ScanfTests, EmptyBufferAndFormats)
    {
        char first_string[64];

        CHECK(__sscanf("", "%s", first_string) == EOF);
        CHECK(__sscanf(NULL, "%s", first_string) == EOF);
        CHECK(__sscanf("Test", "", first_string) == 0);
        CHECK(__sscanf("Test", NULL, first_string) == EOF);
        CHECK(__sscanf("", "") == EOF);
        CHECK(__sscanf(NULL, NULL) == EOF);
    }

    TEST(ScanfTests, SimpleTest)
    {
        char test_char;
        char test_char2;
        char test_string[64];
        int decimal_value;
        unsigned decimal_value2;

        CHECK(__sscanf("Test", "%s", test_string) == 1);
        CHECK(strncmp(test_string, "Test", 64) == 0);

        CHECK(__sscanf("12", "%d", &decimal_value) == 1);
        CHECK(decimal_value == 12);

        CHECK(__sscanf("  TestString   12  ", "%s %d", test_string, &decimal_value) == 2);
        CHECK(strncmp(test_string, "TestString", 64) == 0);
        CHECK(decimal_value == 12);

        CHECK(__sscanf(" A astring", "%c %c %s", &test_char, &test_char2, test_string) == 3);
        CHECK( test_char == ' ' );
        CHECK( test_char2 == 'A' );
        CHECK(strncmp(test_string, "astring", 64) == 0);

        CHECK(__sscanf(" -11  -14  ", "%d %u", &decimal_value, &decimal_value2) == 2);
        CHECK(decimal_value == -11);
        CHECK(decimal_value2 == 4294967282);
    }

    TEST(ScanfTests, HexadecimalTest)
    {
        char char_value;
        int hexadecimal_value;
        int decimal_value;

        CHECK(__sscanf("A02B", "%x", &hexadecimal_value) == 1);
        CHECK(hexadecimal_value == 0xA02B);

        CHECK(__sscanf("00E1A02B", "%X", &hexadecimal_value) == 1);
        CHECK(hexadecimal_value == 0x00E1A02B);

        CHECK(__sscanf("0x12E1A02B", "%X", &hexadecimal_value) == 1);
        CHECK(hexadecimal_value == 0x12E1A02B);

        CHECK(__sscanf("A02Bq12", "%x%c%d", &hexadecimal_value, &char_value, &decimal_value) == 3);
        CHECK(hexadecimal_value == 0xA02B);
        CHECK(char_value == 'q');
        CHECK(decimal_value == 12);
    }

    TEST(ScanfTests, OctalTest)
    {
        char char_value;
        int octal_value;
        int decimal_value;

        CHECK(__sscanf("1234", "%o", &octal_value) == 1);
        CHECK(octal_value == 01234);

        CHECK(__sscanf("1234567812", "%o%c%d", &octal_value, &char_value, &decimal_value) == 3);
        CHECK(octal_value == 01234567);
        CHECK(char_value == '8');
        CHECK(decimal_value == 12);
    }

    TEST(ScanfTests, iTest)
    {
        int octal_value;
        int decimal_value;
        int hexadecimal_value;

        CHECK(__sscanf("1234 0234 0x89ab", "%i %i %i", &decimal_value, &octal_value, &hexadecimal_value) == 3);
        CHECK(decimal_value == 1234);
        CHECK(octal_value == 0234);
        CHECK(hexadecimal_value == 0x89AB);
    }

    TEST(ScanfTests, LongArgument)
    {
        long decimal_value;
        long hexadecimal_value;
        long octal_value;

        CHECK(__sscanf("123", "%ld", &decimal_value) == 1);
        CHECK(decimal_value == 123);

        CHECK(__sscanf("456 0x12E1A02B 1234", "%ld%lX%lo", &decimal_value, &hexadecimal_value, &octal_value) == 3);
        CHECK(decimal_value == 456);
        CHECK(hexadecimal_value == 0x12E1A02B);
        CHECK(octal_value == 01234);
    }

    TEST(ScanfTests, SuppressAssignment)
    {
        char test_string[64];
        int decimal_value;

        CHECK(__sscanf("Test", "%s", test_string) == 1);
        CHECK(strncmp(test_string, "Test", 64) == 0);

        CHECK(__sscanf("12", "%d", &decimal_value) == 1);
        CHECK(decimal_value == 12);

        CHECK(__sscanf("  one  two   12   13  ", "%*s %s %*d %d", test_string, &decimal_value) == 2);
        CHECK(strncmp(test_string, "two", 64) == 0);
        CHECK(decimal_value == 13);
    }

    TEST(ScanfTests, TestMaxFieldLength)
    {
        char test_string[64];
        int decimal_value1;
        int decimal_value2;

        CHECK(__sscanf(" abcdefghijklmnopqrstuvwxyz 1234567 ", "%10s %*s %4d %d", test_string, &decimal_value1, &decimal_value2) == 3);
        CHECK(strncmp(test_string, "abcdefghij", 64) == 0);
        CHECK(decimal_value1 == 1234);
        CHECK(decimal_value2 == 567);
    }

    TEST(ScanfTests, TestMatchSet)
    {
        char test_string[64];
        char test_string2[64];
        int decimal_value;

        CHECK(__sscanf("twghiijkuv 1234 ", "%[tuvwghijk] %d", test_string, &decimal_value) == 2);
        CHECK(strncmp(test_string, "twghiijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        CHECK(__sscanf("   twghiaijkuv 1234 ", " %[tuvwghijk] %s %d", test_string, test_string2, &decimal_value) == 3);
        CHECK(strncmp(test_string, "twghi", 64) == 0);
        CHECK(strncmp(test_string2, "aijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        CHECK(__sscanf(" twghicijkuv 1234 ", " %[^abcdef] %s %d", test_string, test_string2, &decimal_value) == 3);
        CHECK(strncmp(test_string, "twghi", 64) == 0);
        CHECK(strncmp(test_string2, "cijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        //  Test with closing square bracket

        CHECK(__sscanf(" twghi]aijkuv 1234 ", " %[]tuvwghijk] %s %d", test_string, test_string2, &decimal_value) == 3);
        CHECK(strncmp(test_string, "twghi]", 64) == 0);
        CHECK(strncmp(test_string2, "aijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        CHECK(__sscanf(" twghi]ijkuv 1234 ", " %[^]abcdef] %s %d", test_string, test_string2, &decimal_value) == 3);
        CHECK(strncmp(test_string, "twghi", 64) == 0);
        CHECK(strncmp(test_string2, "]ijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        //  Test with space

        CHECK(__sscanf(" twgh iaijkuv 1234 ", " %[tuvwghij k] %s %d", test_string, test_string2, &decimal_value) == 3);
        CHECK(strncmp(test_string, "twgh i", 64) == 0);
        CHECK(strncmp(test_string2, "aijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        CHECK(__sscanf(" twghi ijkuv 1234 ", " %[^abcde f] %s %d", test_string, test_string2, &decimal_value) == 3);
        CHECK(strncmp(test_string, "twghi", 64) == 0);
        CHECK(strncmp(test_string2, "ijkuv", 64) == 0);
        CHECK(decimal_value == 1234);

        //  Error conditions

        CHECK(__sscanf("twghiijkuv 1234 ", "%[", test_string, &decimal_value) == EOF);
        CHECK(__sscanf("twghiijkuv 1234 ", "%[]", test_string, &decimal_value) == EOF);
        CHECK(__sscanf("twghiijkuv 1234 ", "%[] %d", test_string, &decimal_value) == EOF);
        CHECK(__sscanf("twghiijkuv 1234 ", "%[tuvwghijk %d", test_string, &decimal_value) == EOF);
    }
    
    
    TEST(ScanfTests, LiteralsInFormat)
    {
        char test_string[64];
        int decimal_value;

        CHECK(__sscanf("  Test aftertest", " Test%s", test_string) == 1);
        CHECK(strncmp(test_string, "aftertest", 64) == 0);

        CHECK(__sscanf("  Test aftertest2", " Test  %s", test_string) == 1);
        CHECK(strncmp(test_string, "aftertest2", 64) == 0);

        CHECK(__sscanf("  Test Test2 aftertest3", "   Test   Test2   %s", test_string) == 1);
        CHECK(strncmp(test_string, "aftertest3", 64) == 0);

        CHECK(__sscanf("  Test    Test2    aftertest4", " Test Test2 %s", test_string) == 1);
        CHECK(strncmp(test_string, "aftertest4", 64) == 0);

        CHECK(__sscanf("abc12defanotherstring 'this should be skipped'", "abc%ddef%s", &decimal_value, test_string) == 2);
        CHECK(decimal_value == 12);
        CHECK(strncmp(test_string, "anotherstring", 64) == 0);

        CHECK(__sscanf("abc 12 defanotherstring 'this should be skipped'", "abc %d def%s", &decimal_value, test_string) == 2);
        CHECK(decimal_value == 12);
        CHECK(strncmp(test_string, "anotherstring", 64) == 0);

        CHECK(__sscanf("  Test aftertest", "Test %s", test_string) == 0);
    }

}