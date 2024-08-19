// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <fixed_string>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FixedStringTests)
    {
    };
#pragma GCC diagnostic pop

    using fixed_string = minstd::fixed_string<>;

    using fixed_string8 = minstd::fixed_string<8>;
    using fixed_string16 = minstd::fixed_string<16>;
    using fixed_string128 = minstd::fixed_string<128>;

    void BasicStringTest(minstd::string &test_string, const minstd::string &test_string2)
    {
        CHECK_EQUAL(test_string.size(), 11);
        CHECK(test_string == "Test String");
        CHECK(test_string != "Test StrinG");

        test_string = test_string2;
        CHECK(test_string == test_string2);

        fixed_string test_string3(test_string);
        CHECK(test_string3 == test_string2);
    }

    void TestDifferingLengthStrings(minstd::string &test_short_string,
                                    minstd::string &test_medium_string)
    {
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");
        CHECK(test_medium_string.length() == 10);

        test_short_string = test_medium_string;
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");

        test_short_string = fixed_string8("abcdefghijkl", 4);
        CHECK(test_short_string.length() == 4);
        CHECK(test_short_string == "abcd");
        CHECK(test_short_string != "abc");
        CHECK(test_short_string != "abcde");

        test_short_string = fixed_string8("abcdefghijkl", 10);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");

        test_short_string = fixed_string8("abcdefghijkl", 8);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");
    }

    void TestConstructorFromCharPointer(const char *value)
    {
        fixed_string test_string(value);

        CHECK(test_string == "test char pointer");
    }

    TEST(FixedStringTests, BasicCharTest)
    {
        fixed_string test_string = "Test String";
        fixed_string test_string2 = "Test String";

        CHECK(test_string.size() == 11);
        CHECK(test_string == "Test String");
        CHECK(test_string != "Test StrinG");
        CHECK(test_string == test_string2);
    }

    TEST(FixedStringTests, EmptyStringTest)
    {
        fixed_string test_string;

        CHECK(test_string.empty());

        test_string = "not empty";

        CHECK(!test_string.empty());

        test_string = "";

        CHECK(test_string.empty());
    }

    TEST(FixedStringTests, DifferentMaxLengthsTest)
    {
        fixed_string test_string = "Test String";
        fixed_string128 test_string2 = "Test String";

        CHECK(test_string.size() == 11);
        CHECK(test_string == "Test String");
        CHECK(test_string != "Test StrinG");
        CHECK(test_string == test_string2);

        fixed_string8 test_short_string = "0123456789";
        fixed_string16 test_medium_string = "0123456789";

        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");
        CHECK(test_medium_string.length() == 10);

        test_short_string = test_medium_string;
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");

        test_short_string = fixed_string8("abcdefghijkl", 4);
        CHECK(test_short_string.length() == 4);
        CHECK(test_short_string == "abcd");
        CHECK(test_short_string != "abc");
        CHECK(test_short_string != "abcde");

        test_short_string = fixed_string8("abcdefghijkl", 10);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");

        test_short_string = fixed_string8("abcdefghijkl", 8);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");
    }

    TEST(FixedStringTests, IndexingTest)
    {
        fixed_string test_string1 = "abcdefg";

        CHECK(test_string1[0] == 'a');
        CHECK(test_string1[3] == 'd');
        CHECK(test_string1[6] == 'g');
        CHECK(test_string1[7] == 0x00);

        const fixed_string test_string2("hijklmn");

        CHECK(test_string2[0] == 'h');
        CHECK(test_string2[3] == 'k');
        CHECK(test_string2[6] == 'n');
        CHECK(test_string2[7] == 0x00);
    }

    TEST(FixedStringTests, ClearTest)
    {
        fixed_string test_string1 = "abcdefg";

        CHECK(!test_string1.empty());
        STRCMP_EQUAL("abcdefg", test_string1.c_str());

        test_string1.clear();

        CHECK(test_string1.empty());
    }

    TEST(FixedStringTests, PushPopBackTest)
    {
        fixed_string test_string1 = "abcdefg";

        CHECK_EQUAL(test_string1.back(), 'g');

        CHECK(test_string1.push_back('x'));
        CHECK(test_string1.push_back('y'));
        CHECK(test_string1.push_back('z'));
        STRCMP_EQUAL("abcdefgxyz", test_string1.c_str());

        test_string1.pop_back();
        CHECK_EQUAL(test_string1.back(), 'y');
        STRCMP_EQUAL("abcdefgxy", test_string1.c_str());

        fixed_string empty_string;

        CHECK_EQUAL(empty_string.back(), 0x00);

        minstd::fixed_string<4> short_string("ABCD");

        CHECK_EQUAL(short_string.back(), 'D');
        CHECK(!short_string.push_back('E'));
        short_string.pop_back();
        CHECK(short_string.push_back(0x00));
        STRCMP_EQUAL("ABC", short_string.c_str());
    }

    TEST(FixedStringTests, AppendTest)
    {
        fixed_string8 test_short_string = "0123";
        fixed_string16 test_medium_string = "0123456789";

        test_short_string += "45";

        CHECK(test_short_string.length() == 6);
        CHECK(test_short_string == "012345");

        test_short_string += "6789";

        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");
        CHECK(test_short_string != "0123456");
        CHECK(test_short_string != "012345678");
    }

    TEST(FixedStringTests, SearchTest)
    {
        fixed_string test_string = "abcd/efgh/ijkl/mnop";

        CHECK(test_string.find_last_of('/') == 14);
        CHECK(test_string.find_last_of('/', 13) == 9);
        CHECK(test_string.find_last_of('/', 14) == 14);
        CHECK(test_string.find_last_of('p') == 18);
        CHECK(test_string.find_last_of('p', test_string.length()) == 18);
        CHECK(test_string.find_last_of('p', 1000) == 18);
        CHECK(test_string.find_last_of('a') == 0);

        CHECK(test_string.find_last_of('z') == minstd::string::npos);
    }

    TEST(FixedStringTests, BasicTests)
    {
        {
            fixed_string test_string = "Test String";
            fixed_string test_string2 = "Second Test String";

            BasicStringTest(test_string, test_string2);
        }

        {
            fixed_string8 test_short_string = "0123456789";
            fixed_string16 test_medium_string = "0123456789";

            TestDifferingLengthStrings(test_short_string, test_medium_string);

            CHECK(test_short_string == "abcdefgh");
        }

        {
            TestConstructorFromCharPointer("test char pointer");
        }
    }

    TEST(FixedStringTests, FindTest)
    {
        minstd::fixed_string<256> gettysburg = "Four score and seven years ago our fathers brought forth on this continent, a new nation, conceived in Liberty, and dedicated to the proposition that all men are created equal.";

        CHECK(gettysburg.find("Four score") == 0);
        CHECK(gettysburg.find("Not in address") == minstd::string::npos);
        CHECK(gettysburg.find("our fathers") == 31);
        CHECK(gettysburg.find("equal.") == 170);

        minstd::fixed_string<64> char_set = "*:/<>?\\|";

        CHECK(char_set.find('a') == minstd::string::npos);
        CHECK(char_set.find('<') == 3);
        CHECK(char_set.find('\\') == 6);
        CHECK(char_set.find('|') == 7);
    }

    TEST(FixedStringTests, SubstrTest)
    {
        minstd::fixed_string<64> test_string = "0123456789abcdefghij";

        fixed_string16 substring;

        test_string.substr(substring, 10);

        CHECK(substring.size() == 10);
        CHECK(substring == "abcdefghij");

        test_string.substr(substring, 5, 3);

        CHECK(substring.size() == 3);
        CHECK(substring == "567");

        test_string.substr(substring, test_string.size() - 3, 50);

        CHECK(substring.size() == 3);
        CHECK(substring == "hij");

        fixed_string8 substring2;

        test_string.substr(substring2, 10);

        CHECK(substring2.size() == 8);
        CHECK(substring2 == "abcdefgh");

        test_string.substr(substring2, 0, 7);

        CHECK(substring2.size() == 7);
        CHECK(substring2 == "0123456");

        test_string.substr(substring2, 2, 11);

        CHECK(substring2.size() == 8);
        CHECK(substring2 == "23456789");
    }

    TEST(FixedStringTests, EraseTest)
    {
        minstd::fixed_string<64> test_string = "0123456789abcdefghij";

        test_string.erase(2);

        CHECK(test_string.size() == 2);
        CHECK(test_string == "01");

        test_string = "0123456789abcdefghij";

        test_string.erase(3, 3);

        CHECK(test_string.size() == 17);
        CHECK(test_string == "0126789abcdefghij");

        test_string = "0123456789abcdefghij";

        test_string.erase(0);

        CHECK(test_string.empty());
        CHECK(test_string == "");

        test_string = "0123456789abcdefghij";

        test_string.erase(32);

        CHECK(test_string.size() == 20);
        CHECK(test_string == "0123456789abcdefghij");

        test_string = "0123456789abcdefghij";

        test_string.erase(5, 19);

        CHECK(test_string.size() == 5);
        CHECK(test_string == "01234");
    }
}
