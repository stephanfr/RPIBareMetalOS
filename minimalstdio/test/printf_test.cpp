// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

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
#include <iostream>

#include "printf.h"

char test_buffer[256];
char *current_buffer_char = NULL;

extern "C" void putchar_(char c)
{
    *current_buffer_char++ = c;
}

namespace
{
    TEST_CASE("printf - Basic Tests", "")
    {
        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test") == 4);
        REQUIRE(strncmp(test_buffer, "Test", 64) == 0);

        //  Test:
        //  
        //  %s   - string
        //  %c   - character
        //  %d   - int
        //  %u   - unsigned int
        //  %ld  - long
        //  %hd  - short
        //  %lld - long long
        //  %o   - octal
        //  %x   - hexadecimal lower case alphas
        //  %X   - hexadecimal upper case alphas

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %s %c %d %u %ld %hd %lld %o %x %X", "Case", 'a', int(1), unsigned(2), long(3), short(4), uint64_t(5), int(10), int(12), int(255)) == 29);
        test_buffer[29] = 0;
        REQUIRE(strncmp(test_buffer, "Test Case a 1 2 3 4 5 12 c FF", 64) == 0);
    }

    TEST_CASE("printf - Obscure Tests", "")
    {
        //  Test:
        //  
        //  %%  - % 
        //  %b  - binary, I think this is non-standard
        //  %zi - size_t integer
        //  %jd - intmax_t
        //  %td - ptrdiff_t

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %% %zi %b %jd %td", size_t(1), int(12), intmax_t(13), ptrdiff_t(14)) == 19 );
        test_buffer[19] = 0;
        REQUIRE(strncmp(test_buffer, "Test % 1 1100 13 14", 64) == 0);

        //  %p  - pointer

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %p", test_buffer) == 23 );
        test_buffer[23] = 0;
        REQUIRE(strncmp(test_buffer, "Test 0x", 7) == 0);   //  No easy way to confirm this...
    }

    TEST_CASE("printf - Floating Point Tests", "")
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
        REQUIRE(printf_("Test %f %F %e %E %g %G", double(1.1), double(1), double(392.65), double(456.789), double(1), double(456.789)) == 58 );
        test_buffer[58] = 0;
        REQUIRE(strncmp(test_buffer, "Test 1.100000 1.000000 3.926500e+02 4.567890E+02 1 456.789", 64) == 0);

        //  Rollover tests

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %.0f %.0f %.0f %#.0f", double(0.99), double(1.5), double(2.5), double(0.99)) == 13 );
        test_buffer[13] = 0;
        REQUIRE(strncmp(test_buffer, "Test 1 2 2 1.", 64) == 0);


    }

    TEST_CASE("printf - Width and Flag Tests", "")
    {
        //  Test:
        //  
        //  %10d  - decimal ten digits space padded
        //  %010d - decimal ten digits with leading zeros
        //  %-10d - decimal ten digits left justified space padded
        //  %+d   - decimal with leading +/-
        //  % d   - decimal with leading space for positive values

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %10d %010d %-10d %+d % d", int(1977), int(1977), int(1977), int(1977), int(1977) ) == 49 );
        test_buffer[49] = 0;
        REQUIRE(strncmp(test_buffer, "Test       1977 0000001977 1977       +1977  1977", 64) == 0);

        //  %2d   - decimal maximum of 2 digits or the length of the numeric value

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %2d", int(-1977) ) == 10 );
        test_buffer[10] = 0;
        REQUIRE(strncmp(test_buffer, "Test -1977", 64) == 0);

        //  %08o   - octal maximum of 8 digits zero padded

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %08o", int(1977) ) == 13 );
        test_buffer[13] = 0;
        REQUIRE(strncmp(test_buffer, "Test 00003671", 64) == 0);

        //  %012f   - floating point maximum of 12 digits or the length of the numeric value zero padded
        //  %+012f  - floating point maximum of 12 digits or the length of the numeric value zero padded
        //  %.4g    - floating point general no trailing zeros

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %012f %+012f %.4g", double(-197), double(197), double(15) ) == 33 );
        test_buffer[33] = 0;
        REQUIRE(strncmp(test_buffer, "Test -0197.000000 +0197.000000 15", 64) == 0);

        //  %2s   - string maximum of 2 characters or the length of the string
        //  %8s   - string maximum of 8 characters padded with spaces

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %2s %8s", "Case", "Width" ) == 18 );
        test_buffer[18] = 0;
        REQUIRE(strncmp(test_buffer, "Test Case    Width", 64) == 0);
    }

    TEST_CASE("printf - Precision Tests", "")
    {
        //  Test:
        //  
        //  %.7d  - decimal 7 digits zero padded
        //  %.7o  - octal 7 digits zero padded
        //  %.7x  - hex 7 digits zero padded
        //  %.4f  - floating point, four digits after the decimal point zero padded

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %.7d %.7o %.7x %.4f", int(-1977), int(1977), int(1977), double(-1.234) ) == 37 );
        test_buffer[37] = 0;
        REQUIRE(strncmp(test_buffer, "Test -0001977 0003671 00007b9 -1.2340", 64) == 0);

        //  %.2s   - string maximum of 2 characters
        //  %.8s   - string maximum of 8 characters

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %.2s %.8s", "Case", "Width" ) == 13 );
        test_buffer[13] = 0;
        REQUIRE(strncmp(test_buffer, "Test Ca Width", 64) == 0);
    }

    TEST_CASE("printf - Hash Tests", "")
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
        REQUIRE(printf_("Test %#o %#b %#x %#X %#g %#10o %#010x", int(9), int(6), int(13), int(1023), double(1), int(256), int(257)) == 54 );
        test_buffer[54] = 0;
        REQUIRE(strncmp(test_buffer, "Test 011 0b110 0xd 0X3FF 1.00000       0400 0x00000101", 64) == 0);
    }

    TEST_CASE("printf - Asterisk Tests", "")
    {
        //  Test:
        //  
        //  %*i  - decimal with width in the argument list
        //  %.*i - decimal with precision in the argument list

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %*i %.*i", 8, 1964, 6, 1977) == 20 );
        test_buffer[20] = 0;
        REQUIRE(strncmp(test_buffer, "Test     1964 001977", 64) == 0);
    }

    TEST_CASE("printf - Writeback Tests", "")
    {
        //  Test:
        //  
        //  %n - number of processed characters written back to the argument 

        int char_count1 = -1;
        int char_count2 = -1;

        current_buffer_char = test_buffer;
        REQUIRE(printf_("Test %n Writeback %n", &char_count1, &char_count2) == 16 );
        test_buffer[16] = 0;
        REQUIRE(strncmp(test_buffer, "Test  Writeback ", 64) == 0);
        REQUIRE(char_count1 == 5);
        REQUIRE(char_count2 == 16);
    }

    TEST_CASE("sprintf - Basic Tests", "")
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

        REQUIRE(sprintf_( local_buffer, "Test %s %c %d %u %ld %o %x %X", "Case", 'a', int(1), unsigned(2), long(3), int(10), int(12), int(255)) == 25);
        local_buffer[25] = 0;
        REQUIRE(strncmp(local_buffer, "Test Case a 1 2 3 12 c FF", 64) == 0);

        for( int i = 0; i < 256; i++)
        {
            local_buffer[i] = 0;
        }

        REQUIRE(snprintf_( local_buffer, 16, "Test %s %c %d %u %ld %o %x %X", "Case", 'a', int(1), unsigned(2), long(3), int(10), int(12), int(255)) == 25);
        REQUIRE(strncmp(local_buffer, "Test Case a 1 2", 64) == 0);
    }

}
