// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <dynamic_string>
#include <fixed_string>
#include <heap_allocator>
#include <single_block_memory_heap>

#define TEST_BUFFER_SIZE 65536

namespace
{

    static char buffer[TEST_BUFFER_SIZE];

    using dynamic_string = minstd::dynamic_string<>;

    using dynamic_string8 = minstd::dynamic_string<8>;
    using dynamic_string16 = minstd::dynamic_string<16>;
    using dynamic_string128 = minstd::dynamic_string<128>;

    using DynamicStringHeapAllocator = minstd::heap_allocator<char>;

    minstd::single_block_memory_heap test_heap(buffer, 4096);
    DynamicStringHeapAllocator heap_allocator(test_heap);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (DynamicStringTests)
    {
        void setup()
        {
            CHECK_EQUAL(0, test_heap.bytes_in_use());
        }

        void teardown()
        {
            CHECK_EQUAL(0, test_heap.bytes_in_use());
        }
    };
#pragma GCC diagnostic pop

    void BasicStringTest(minstd::string &test_string, const minstd::string &test_string2, DynamicStringHeapAllocator allocator)
    {
        CHECK_EQUAL(test_string.size(), 11);
        CHECK(test_string == "Test String");
        CHECK(test_string != "Test StrinG");

        test_string = test_string2;
        CHECK(test_string == test_string2);

        dynamic_string test_string3(test_string, allocator);
        CHECK(test_string3 == test_string2);

        char test_buffer[16] = "/";
        dynamic_string test_string4(test_buffer, 1, allocator);
        CHECK(test_string4 == "/");
    }

    void TestDifferingLengthStrings(minstd::string &test_short_string,
                                    minstd::string &test_medium_string,
                                    DynamicStringHeapAllocator allocator)
    {
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");
        CHECK(test_medium_string.length() == 10);

        test_short_string = test_medium_string;
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");

        test_short_string = dynamic_string8("abcdefghijkl", 4, allocator);
        CHECK(test_short_string.length() == 4);
        CHECK(test_short_string == "abcd");
        CHECK(test_short_string != "abc");
        CHECK(test_short_string != "abcde");

        test_short_string = dynamic_string8("abcdefghijkl", 10, allocator);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");

        test_short_string = dynamic_string8("abcdefghijkl", 8, allocator);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");
    }

    void TestConstructorFromCharPointer(const char *value, DynamicStringHeapAllocator allocator)
    {
        dynamic_string test_string(value, allocator);

        CHECK(test_string == "test char pointer");
    }

    void AssignToBasicString(minstd::string &basic, const char *value)
    {
        basic = value;
    }

    TEST(DynamicStringTests, BasicCharTest)
    {
        dynamic_string test_string{"Test String", heap_allocator};
        dynamic_string test_string2{"Test String", heap_allocator};

        CHECK(test_string.size() == 11);
        CHECK(test_string == "Test String");
        CHECK(test_string != "Test StrinG");
        CHECK(test_string == test_string2);
    }

    TEST(DynamicStringTests, EmptyStringTest)
    {
        dynamic_string test_string(heap_allocator);

        CHECK(test_string.empty());

        test_string = "not empty";

        CHECK(!test_string.empty());

        test_string = "";

        CHECK(test_string.empty());
    }

    TEST(DynamicStringTests, DifferentMaxLengthsTest)
    {
        dynamic_string test_string("Test String", heap_allocator);
        dynamic_string128 test_string2("Test String", heap_allocator);

        CHECK(test_string.size() == 11);
        CHECK(test_string == "Test String");
        CHECK(test_string != "Test StrinG");
        CHECK(test_string == test_string2);

        dynamic_string8 test_short_string("0123456789", heap_allocator);
        dynamic_string16 test_medium_string("0123456789", heap_allocator);

        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");
        CHECK(test_medium_string.length() == 10);

        test_short_string = test_medium_string;
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");

        test_short_string = dynamic_string8("abcdefghijkl", 4, heap_allocator);
        CHECK(test_short_string.length() == 4);
        CHECK(test_short_string == "abcd");
        CHECK(test_short_string != "abc");
        CHECK(test_short_string != "abcde");

        test_short_string = dynamic_string8("abcdefghijkl", 10, heap_allocator);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");

        test_short_string = dynamic_string8("abcdefghijkl", 8, heap_allocator);
        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "abcdefgh");
        CHECK(test_short_string != "abcdefg");
        CHECK(test_short_string != "abcdefghi");
    }

    TEST(DynamicStringTests, IndexingTest)
    {
        dynamic_string test_string1("abcdefg", heap_allocator);

        CHECK(test_string1[0] == 'a');
        CHECK(test_string1[3] == 'd');
        CHECK(test_string1[6] == 'g');
        CHECK(test_string1[7] == 0x00);

        const dynamic_string test_string2("hijklmn", heap_allocator);

        CHECK(test_string2[0] == 'h');
        CHECK(test_string2[3] == 'k');
        CHECK(test_string2[6] == 'n');
        CHECK(test_string2[7] == 0x00);
    }

    TEST(DynamicStringTests, ClearTest)
    {
        dynamic_string test_string1("abcdefg", heap_allocator);

        CHECK(!test_string1.empty());
        STRCMP_EQUAL("abcdefg", test_string1.c_str());

        test_string1.clear();

        CHECK(test_string1.empty());
    }

    TEST(DynamicStringTests, PushPopBackTest)
    {
        dynamic_string test_string1("abcdefg", heap_allocator);

        CHECK_EQUAL(test_string1.back(), 'g');

        CHECK(test_string1.push_back('x'));
        CHECK(test_string1.push_back('y'));
        CHECK(test_string1.push_back('z'));
        STRCMP_EQUAL("abcdefgxyz", test_string1.c_str());

        test_string1.pop_back();
        CHECK_EQUAL(test_string1.back(), 'y');
        STRCMP_EQUAL("abcdefgxy", test_string1.c_str());

        dynamic_string empty_string(heap_allocator);

        CHECK_EQUAL(empty_string.back(), 0x00);

        minstd::dynamic_string<4> short_string("ABCD", heap_allocator);

        CHECK_EQUAL(short_string.back(), 'D');
        CHECK(!short_string.push_back('E'));
        short_string.pop_back();
        CHECK(short_string.push_back(0x00));
        STRCMP_EQUAL("ABC", short_string.c_str());
    }

    TEST(DynamicStringTests, AppendTest)
    {
        dynamic_string8 test_short_string("0123", heap_allocator);
        dynamic_string16 test_medium_string("0123456789", heap_allocator);

        test_short_string += "45";

        CHECK(test_short_string.length() == 6);
        CHECK(test_short_string == "012345");

        test_short_string += "6789";

        CHECK(test_short_string.length() == 8);
        CHECK(test_short_string == "01234567");
        CHECK(test_short_string != "0123456");
        CHECK(test_short_string != "012345678");
    }

    TEST(DynamicStringTests, SearchTest)
    {
        dynamic_string test_string("abcd/efgh/ijkl/mnop", heap_allocator);

        CHECK(test_string.find_last_of('/') == 14);
        CHECK(test_string.find_last_of('/', 13) == 9);
        CHECK(test_string.find_last_of('/', 14) == 14);
        CHECK(test_string.find_last_of('p') == 18);
        CHECK(test_string.find_last_of('p', test_string.length()) == 18);
        CHECK(test_string.find_last_of('p', 1000) == 18);
        CHECK(test_string.find_last_of('a') == 0);
    }

    TEST(DynamicStringTests, BasicTests)
    {
        {
            dynamic_string test_string("Test String", heap_allocator);
            dynamic_string test_string2("Second Test String", heap_allocator);

            BasicStringTest(test_string, test_string2, heap_allocator);
        }

        {
            dynamic_string8 test_short_string("0123456789", heap_allocator);
            dynamic_string16 test_medium_string("0123456789", heap_allocator);

            TestDifferingLengthStrings(test_short_string, test_medium_string, heap_allocator);

            CHECK(test_short_string == "abcdefgh");
        }

        {
            TestConstructorFromCharPointer("test char pointer", heap_allocator);
        }
    }

    TEST(DynamicStringTests, AssignmentTest)
    {
        {
            dynamic_string test_string(heap_allocator);
            dynamic_string long_test_string("Four score and seven years ago our fathers brought forth on this continent, a new nation, conceived in Liberty, and dedicated to the proposition that all men are created equal.", heap_allocator);

            test_string = long_test_string;

            CHECK(test_string == long_test_string);
        }

        {
            dynamic_string test_string(heap_allocator);
            minstd::fixed_string<256> long_fixed_string = "Now we are engaged in a great civil war, testing whether that nation, or any nation so conceived and so dedicated, can long endure.";

            test_string = long_fixed_string;

            CHECK(test_string == long_fixed_string);
        }

        {
            dynamic_string test_string(heap_allocator);

            test_string = "We have come to dedicate a portion of that field, as a final resting place for those who here gave their lives that that nation might live.";

            CHECK(test_string == "We have come to dedicate a portion of that field, as a final resting place for those who here gave their lives that that nation might live.");
        }

        {
            dynamic_string test_string(heap_allocator);

            AssignToBasicString(test_string, "We have come to dedicate a portion of that field, as a final resting place for those who here gave their lives that that nation might live.");

            CHECK(test_string == "We have come to dedicate a portion of that field, as a final resting place for those who here gave their lives that that nation might live.");
        }
    }

    TEST(DynamicStringTests, FindTest)
    {
        minstd::dynamic_string<256> gettysburg("Four score and seven years ago our fathers brought forth on this continent, a new nation, conceived in Liberty, and dedicated to the proposition that all men are created equal.", heap_allocator);

        CHECK(gettysburg.find("Four score") == 0);
        CHECK(gettysburg.find("Not in address") == minstd::string::npos);
        CHECK(gettysburg.find("our fathers") == 31);
        CHECK(gettysburg.find("equal.") == 170);

        minstd::dynamic_string<64> char_set("*:/<>?\\| ", heap_allocator);

        CHECK(char_set.find('a') == minstd::string::npos);
        CHECK(char_set.find('<') == 3);
        CHECK(char_set.find('\\') == 6);
        CHECK(char_set.find('|') == 7);
    }

    TEST(DynamicStringTests, SubstrTest)
    {
        minstd::dynamic_string<64> test_string("0123456789abcdefghij", heap_allocator);

        dynamic_string16 substring(heap_allocator);

        test_string.substr(substring, 10);

        CHECK(substring.size() == 10);
        CHECK(substring == "abcdefghij");

        test_string.substr(substring, 5, 3);

        CHECK(substring.size() == 3);
        CHECK(substring == "567");

        test_string.substr(substring, test_string.size() - 3, 50);

        CHECK(substring.size() == 3);
        CHECK(substring == "hij");

        dynamic_string8 substring2(heap_allocator);

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

    TEST(DynamicStringTests, EraseTest)
    {
        minstd::dynamic_string<64> test_string("0123456789abcdefghij", heap_allocator);

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
