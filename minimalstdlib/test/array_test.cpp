// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <stdint.h>

#include <minstdconfig.h>

#include <array>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ArrayTests)
    {};
#pragma GCC diagnostic pop

    class TestElement
    {
    public:
        TestElement()
            : value_(0)
        {
        }

        explicit TestElement(uint32_t value)
            : value_(value)
        {
        }

        TestElement(const TestElement &) = default;

        uint32_t value() const
        {
            return value_;
        }

        uint32_t &value()
        {
            return value_;
        }

    private:
        uint32_t value_;
    };

    TEST(ArrayTests, ZeroLengthArray)
    {
        minstd::array<uint32_t, 0> test_array;

        CHECK_EQUAL(test_array.size(), 0);
        CHECK_EQUAL(test_array.max_size(), 0);
        CHECK_EQUAL(test_array.begin(), test_array.end());
        CHECK(test_array.empty());
    }


    TEST(ArrayTests, TestArrayWithIntrinsic)
    {
        minstd::array<uint32_t, 10> test_array;

        test_array[0] = 10;
        test_array[1] = 11;
        test_array[2] = 12;
        test_array[3] = 13;
        test_array[4] = 14;
        test_array[5] = 15;
        test_array[6] = 16;
        test_array[7] = 17;
        test_array[8] = 18;
        test_array[9] = 19;

        CHECK_EQUAL(test_array.front(), 10);
        CHECK_EQUAL(test_array.back(), 19);
        CHECK_EQUAL(test_array.size(), 10);
        CHECK_EQUAL(test_array.max_size(), 10);
        CHECK(!test_array.empty());

        CHECK_EQUAL(test_array[0], 10);
        CHECK_EQUAL(test_array[1], 11);
        CHECK_EQUAL(test_array[2], 12);
        CHECK_EQUAL(test_array[3], 13);
        CHECK_EQUAL(test_array[4], 14);
        CHECK_EQUAL(test_array[5], 15);
        CHECK_EQUAL(test_array[6], 16);
        CHECK_EQUAL(test_array[7], 17);
        CHECK_EQUAL(test_array[8], 18);
        CHECK_EQUAL(test_array[9], 19);

        size_t value = 10;

        for (minstd::array<uint32_t, 10>::iterator itr = test_array.begin(); itr != test_array.end(); itr++, value++)
        {
            CHECK_EQUAL(*itr, value);
        }
    }

    TEST(ArrayTests, TestArrayWithClass)
    {
        minstd::array<TestElement, 5> test_array;

        test_array[0] = TestElement(110);
        test_array[1] = TestElement(111);
        test_array[2] = TestElement(112);
        test_array[3] = TestElement(113);
        test_array[4] = TestElement(114);

        CHECK_EQUAL(test_array.front().value(), 110);
        CHECK_EQUAL(test_array.back().value(), 114);
        CHECK_EQUAL(test_array.size(), 5);
        CHECK_EQUAL(test_array.max_size(), 5);
        CHECK(!test_array.empty());

        CHECK_EQUAL(test_array[0].value(), 110);
        CHECK_EQUAL(test_array[1].value(), 111);
        CHECK_EQUAL(test_array[2].value(), 112);
        CHECK_EQUAL(test_array[3].value(), 113);
        CHECK_EQUAL(test_array[4].value(), 114);

        size_t value = 110;

        for (minstd::array<TestElement, 5>::iterator itr = test_array.begin(); itr != test_array.end(); itr++, value++)
        {
            CHECK_EQUAL(itr->value(), value);
        }
    }

    TEST(ArrayTests, TestArrayFillWithClass)
    {
        minstd::array<TestElement, 5> test_array;

        test_array.fill(TestElement(200));

        test_array[0].value() += 0;
        test_array[1].value() += 1;
        test_array[2].value() += 2;
        test_array.at(3).value() += 3;
        test_array.at(4).value() += 4;

        CHECK_EQUAL(test_array.front().value(), 200);
        CHECK_EQUAL(test_array.back().value(), 204);
        CHECK_EQUAL(test_array.size(), 5);
        CHECK_EQUAL(test_array.max_size(), 5);
        CHECK(!test_array.empty());

        CHECK_EQUAL(test_array.at(0).value(), 200);
        CHECK_EQUAL(test_array.at(1).value(), 201);
        CHECK_EQUAL(test_array.at(2).value(), 202);
        CHECK_EQUAL(test_array.at(3).value(), 203);
        CHECK_EQUAL(test_array.at(4).value(), 204);

        size_t value = 200;

        for (minstd::array<TestElement, 5>::iterator itr = test_array.begin(); itr != test_array.end(); itr++, value++)
        {
            CHECK_EQUAL(itr->value(), value);
        }
    }
}
