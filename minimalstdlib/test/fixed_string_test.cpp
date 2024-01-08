// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

size_t strlcpy(char *destination, const char *source, size_t max_size);

char	*strnstr(const char *string_to_search, const char *string_to_search_for, size_t max_count); 

#include "../include/fixed_string"

namespace
{
    using fixed_string = minstd::fixed_string<>;

    using fixed_string8 = minstd::fixed_string<8>;
    using fixed_string16 = minstd::fixed_string<16>;
    using fixed_string128 = minstd::fixed_string<128>;

    void BasicStringTest( minstd::string &test_string, const minstd::string &test_string2)
    {
        REQUIRE(test_string.size() == 11);
        REQUIRE(test_string == "Test String");
        REQUIRE(test_string != "Test StrinG");

        test_string = test_string2;
        REQUIRE(test_string == test_string2);

        fixed_string test_string3(test_string);
        REQUIRE( test_string3 == test_string2 );
    }

    void TestDifferingLengthStrings(minstd::string &test_short_string,
                                    minstd::string &test_medium_string)
    {
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "01234567");
        REQUIRE(test_medium_string.length() == 10);

        test_short_string = test_medium_string;
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "01234567");

        test_short_string = fixed_string8("abcdefghijkl", 4);
        REQUIRE(test_short_string.length() == 4);
        REQUIRE(test_short_string == "abcd");
        REQUIRE(test_short_string != "abc");
        REQUIRE(test_short_string != "abcde");

        test_short_string = fixed_string8("abcdefghijkl", 10);
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "abcdefgh");
        REQUIRE(test_short_string != "abcdefg");
        REQUIRE(test_short_string != "abcdefghi");

        test_short_string = fixed_string8("abcdefghijkl", 8);
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "abcdefgh");
        REQUIRE(test_short_string != "abcdefg");
        REQUIRE(test_short_string != "abcdefghi");
    }

    void TestConstructorFromCharPointer( const char*    value )
    {
        fixed_string  test_string(value);

        REQUIRE( test_string == "test char pointer" );
    }


    TEST_CASE("Basic char fixed string test", "")
    {
        fixed_string test_string = "Test String";
        fixed_string test_string2 = "Test String";

        REQUIRE(test_string.size() == 11);
        REQUIRE(test_string == "Test String");
        REQUIRE(test_string != "Test StrinG");
        REQUIRE(test_string == test_string2);
    }

    TEST_CASE("Empty Test", "")
    {
        fixed_string test_string;

        REQUIRE(test_string.empty());

        test_string = "not empty";

        REQUIRE(!test_string.empty());

        test_string = "";

        REQUIRE(test_string.empty());
    }

    TEST_CASE("Different Max Lengths", "")
    {
        fixed_string test_string = "Test String";
        fixed_string128 test_string2 = "Test String";

        REQUIRE(test_string.size() == 11);
        REQUIRE(test_string == "Test String");
        REQUIRE(test_string != "Test StrinG");
        REQUIRE(test_string == test_string2);

        fixed_string8 test_short_string = "0123456789";
        fixed_string16 test_medium_string = "0123456789";

        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "01234567");
        REQUIRE(test_medium_string.length() == 10);

        test_short_string = test_medium_string;
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "01234567");

        test_short_string = fixed_string8("abcdefghijkl", 4);
        REQUIRE(test_short_string.length() == 4);
        REQUIRE(test_short_string == "abcd");
        REQUIRE(test_short_string != "abc");
        REQUIRE(test_short_string != "abcde");

        test_short_string = fixed_string8("abcdefghijkl", 10);
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "abcdefgh");
        REQUIRE(test_short_string != "abcdefg");
        REQUIRE(test_short_string != "abcdefghi");

        test_short_string = fixed_string8("abcdefghijkl", 8);
        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "abcdefgh");
        REQUIRE(test_short_string != "abcdefg");
        REQUIRE(test_short_string != "abcdefghi");
    }

    TEST_CASE("Append", "")
    {
        fixed_string8 test_short_string = "0123";
        fixed_string16 test_medium_string = "0123456789";

        test_short_string += "45";

        REQUIRE(test_short_string.length() == 6);
        REQUIRE(test_short_string == "012345");

        test_short_string += "6789";

        REQUIRE(test_short_string.length() == 8);
        REQUIRE(test_short_string == "01234567");
        REQUIRE(test_short_string != "0123456");
        REQUIRE(test_short_string != "012345678");
    }

    TEST_CASE("Search", "")
    {
        fixed_string test_string = "abcd/efgh/ijkl/mnop";

        REQUIRE(test_string.find_last_of('/') == 14);
        REQUIRE(test_string.find_last_of('/', 13) == 9);
        REQUIRE(test_string.find_last_of('/', 14) == 14);
        REQUIRE(test_string.find_last_of('p') == 18);
        REQUIRE(test_string.find_last_of('p', test_string.length()) == 18);
        REQUIRE(test_string.find_last_of('p', 1000) == 18);
        REQUIRE(test_string.find_last_of('a') == 0);
    }

    TEST_CASE("Basic String Test", "")
    {
        {
            fixed_string test_string = "Test String";
            fixed_string test_string2 = "Second Test String";

            BasicStringTest(test_string, test_string2);
        }

        {
            fixed_string8 test_short_string = "0123456789";
            fixed_string16 test_medium_string = "0123456789";

            TestDifferingLengthStrings( test_short_string, test_medium_string );

            REQUIRE(test_short_string == "abcdefgh");
        }

        {
            TestConstructorFromCharPointer("test char pointer");
        }
    }

    TEST_CASE("Find Test", "")
    {
        minstd::fixed_string<256> gettysburg = "Four score and seven years ago our fathers brought forth on this continent, a new nation, conceived in Liberty, and dedicated to the proposition that all men are created equal.";
    
        REQUIRE( gettysburg.find( "Four score" ) == 0 );
        REQUIRE( gettysburg.find( "Not in address" ) == minstd::string::npos );
        REQUIRE( gettysburg.find("our fathers") == 31 );
        REQUIRE( gettysburg.find("equal.") == 170 );
    }

    TEST_CASE("Substr Test", "")
    {
        minstd::fixed_string<64> test_string = "0123456789abcdefghij";
    
        fixed_string16 substring;
        
        test_string.substr( substring, 10 );

        REQUIRE( substring.size() == 10 );
        REQUIRE( substring == "abcdefghij" );
        
        test_string.substr( substring, 5, 3 );

        REQUIRE( substring.size() == 3 );
        REQUIRE( substring == "567" );
        
        test_string.substr( substring, test_string.size() - 3, 50 );

        REQUIRE( substring.size() == 3 );
        REQUIRE( substring == "hij" );

        fixed_string8 substring2;

        test_string.substr( substring2, 10 );

        REQUIRE( substring2.size() == 8 );
        REQUIRE( substring2 == "abcdefgh" );

        test_string.substr( substring2, 0, 7 );

        REQUIRE( substring2.size() == 7 );
        REQUIRE( substring2 == "0123456" );

        test_string.substr( substring2, 2, 11 );

        REQUIRE( substring2.size() == 8 );
        REQUIRE( substring2 == "23456789" );
    }
}
