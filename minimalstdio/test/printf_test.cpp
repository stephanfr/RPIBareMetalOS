// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <stdint.h>

#include <string.h>

//#define _ISupper _U
//#define _ISlower _L
//#define _ISalpha _U | _L
//#define _ISdigit _N
//#define _ISxdigit _X
//#define _ISspace _S
//#define _ISprint _U | _L | _N | _S | _P
//#define _IScntrl _C
//#define _ISpunct _P
//#define _ISblank _B

#include "printf.h"

char test_buffer[256];
char *current_buffer_char = NULL;

extern "C" void putchar_(char c)
{
    *current_buffer_char++ = c;
}

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(PrintfTests)
    {};
#pragma GCC diagnostic pop

//  GCC does not like the %b format

#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"

    TEST(PrintfTests, BasicTests)
    {
        current_buffer_char = test_buffer;
        CHECK(printf_("Test") == 4);
        STRNCMP_EQUAL(test_buffer, "Test", 4);

        //  Test:
        //  
        //  %s   - string
        //  %c   - character
        //  %d   - int
        //  %u   - unsigned int
        //  %ld  - long
        //  %hd  - short
        //  %o   - octal
        //  %x   - hexadecimal lower case alphas
        //  %X   - hexadecimal upper case alphas

        current_buffer_char = test_buffer;

        CHECK( printf_("Test %s %c %d %u %ld %hd %o %x %X", "Case", 'a', int(1), unsigned(2), long(3), short(4), int(10), int(12), int(255)) == 27);
        STRNCMP_EQUAL( test_buffer, "Test Case a 1 2 3 4 12 c FF", 27 );
    }

    TEST(PrintfTests, ObscureTests)
    {
        //  Test:
        //  
        //  %%  - % 
        //  %b  - binary, I think this is non-standard
        //  %zi - size_t integer
        //  %jd - intmax_t
        //  %td - ptrdiff_t

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %% %zi %b %jd %td", size_t(1), int(12), intmax_t(13), ptrdiff_t(14)) == 19 );
        STRNCMP_EQUAL(test_buffer, "Test % 1 1100 13 14", 19);
        //  %p  - pointer

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %p", test_buffer) == 23 );
        STRNCMP_EQUAL(test_buffer, "Test 0x", 7);   //  No easy way to confirm this...
    }

    TEST(PrintfTests, FloatingPoint)
    {
        //  Test:
        //  
        //  %f - double 
        //  %F - double
        //  %e - exponential with 'e'
        //  %E - exponential with 'E'
        //  %g
        //  %G

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %f %F %e %E %g %G", double(1.1), double(1), double(392.65), double(456.789), double(1), double(456.789)) == 58 );
        STRNCMP_EQUAL(test_buffer, "Test 1.100000 1.000000 3.926500e+02 4.567890E+02 1 456.789", 58);

        //  Rollover tests

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %.0f %.0f %.0f %#.0f", double(0.99), double(1.5), double(2.5), double(0.99)) == 13 );
        STRNCMP_EQUAL(test_buffer, "Test 1 2 2 1.", 13);
    }

    TEST(PrintfTests, WidthAndFlag)
    {
        //  Test:
        //  
        //  %10d  - decimal ten digits space padded
        //  %010d - decimal ten digits with leading zeros
        //  %-10d - decimal ten digits left justified space padded
        //  %+d   - decimal with leading +/-
        //  % d   - decimal with leading space for positive values

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %10d %010d %-10d %+d % d", int(1977), int(1977), int(1977), int(1977), int(1977) ) == 49 );
        STRNCMP_EQUAL(test_buffer, "Test       1977 0000001977 1977       +1977  1977", 49);

        //  %2d   - decimal maximum of 2 digits or the length of the numeric value

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %2d", int(-1977) ) == 10 );
        STRNCMP_EQUAL(test_buffer, "Test -1977", 10);

        //  %08o   - octal maximum of 8 digits zero padded

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %08o", int(1977) ) == 13 );
        STRNCMP_EQUAL(test_buffer, "Test 00003671", 13);

        //  %012f   - floating point maximum of 12 digits or the length of the numeric value zero padded
        //  %+012f  - floating point maximum of 12 digits or the length of the numeric value zero padded
        //  %.4g    - floating point general no trailing zeros

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %012f %+012f %.4g", double(-197), double(197), double(15) ) == 33 );
        STRNCMP_EQUAL(test_buffer, "Test -0197.000000 +0197.000000 15", 33);

        //  %2s   - string maximum of 2 characters or the length of the string
        //  %8s   - string maximum of 8 characters padded with spaces

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %2s %8s", "Case", "Width" ) == 18 );
        STRNCMP_EQUAL(test_buffer, "Test Case    Width", 18);
    }

    TEST(PrintfTests, Precision)
    {
        //  Test:
        //  
        //  %.7d  - decimal 7 digits zero padded
        //  %.7o  - octal 7 digits zero padded
        //  %.7x  - hex 7 digits zero padded
        //  %.4f  - floating point, four digits after the decimal point zero padded

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %.7d %.7o %.7x %.4f", int(-1977), int(1977), int(1977), double(-1.234) ) == 37 );
        STRNCMP_EQUAL(test_buffer, "Test -0001977 0003671 00007b9 -1.2340", 37);

        //  %.2s   - string maximum of 2 characters
        //  %.8s   - string maximum of 8 characters

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %.2s %.8s", "Case", "Width" ) == 13 );
        STRNCMP_EQUAL(test_buffer, "Test Ca Width", 13);
    }

    TEST(PrintfTests, Hash)
    {
        //  Test:
        //  
        //  %#o - octal with '0' prefix 
        //  %#b - binary with 'b' prefix
        //  %#x - hex with '0x' prefix
        //  %#X - hex with '0X' prefix
        //  %#g - floating point with decimal point
        //  %#10o - octal with prefix and 10 character width
        //  %#010x - hex with prefix and 10 character width with zero padding

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %#o %#b %#x %#X %#g %#10o %#010x", int(9), int(6), int(13), int(1023), double(1), int(256), int(257)) == 54 );
        STRNCMP_EQUAL(test_buffer, "Test 011 0b110 0xd 0X3FF 1.00000       0400 0x00000101", 54);
    }

    TEST(PrintfTests, Asterisk)
    {
        //  Test:
        //  
        //  %*i  - decimal with width in the argument list
        //  %.*i - decimal with precision in the argument list

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %*i %.*i", 8, 1964, 6, 1977) == 20 );
        STRNCMP_EQUAL(test_buffer, "Test     1964 001977", 20);
    }

    TEST(PrintfTests, Writeback)
    {
        //  Test:
        //  
        //  %n - number of processed characters written back to the argument 

        int char_count1 = -1;
        int char_count2 = -1;

        current_buffer_char = test_buffer;
        CHECK(printf_("Test %n Writeback %n", &char_count1, &char_count2) == 16 );
        STRNCMP_EQUAL(test_buffer, "Test  Writeback ", 16);
        CHECK(char_count1 == 5);
        CHECK(char_count2 == 16);
    }

    TEST(PrintfTests, sprintfBasicTests)
    {
        //  Test:
        //  
        //  %s  - string
        //  %c  - character
        //  %d  - int
        //  %u  - unsigned int
        //  %ld - long
        //  %o  - octal
        //  %x  - hexadecimal lower case alphas
        //  %X  - hexadecimal upper case alphas

        char local_buffer[256];

        CHECK(sprintf_( local_buffer, "Test %s %c %d %u %ld %o %x %X", "Case", 'a', int(1), unsigned(2), long(3), int(10), int(12), int(255)) == 25);
        STRNCMP_EQUAL(local_buffer, "Test Case a 1 2 3 12 c FF", 25);

        for( int i = 0; i < 256; i++)
        {
            local_buffer[i] = 0;
        }

        CHECK(snprintf_( local_buffer, 16, "Test %s %c %d %u %ld %o %x %X", "Case", 'a', int(1), unsigned(2), long(3), int(10), int(12), int(255)) == 25);
        STRNCMP_EQUAL(local_buffer, "Test Case a 1 2", 25);
    }
}
