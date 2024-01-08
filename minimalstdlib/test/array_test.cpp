// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"

#include "../include/array"

namespace
{

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

    TEST_CASE("Test zero length array", "")
    {
        minstd::array<uint32_t, 0> test_array;

        REQUIRE(test_array.size() == 0);
        REQUIRE(test_array.max_size() == 0);
        REQUIRE(test_array.begin() == test_array.end());
        REQUIRE(test_array.empty());
    }

    TEST_CASE("Test array with intrinsic", "")
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

        REQUIRE(test_array.front() == 10);
        REQUIRE(test_array.back() == 19);
        REQUIRE(test_array.size() == 10);
        REQUIRE(test_array.max_size() == 10);
        REQUIRE(!test_array.empty());

        REQUIRE(test_array[0] == 10);
        REQUIRE(test_array[1] == 11);
        REQUIRE(test_array[2] == 12);
        REQUIRE(test_array[3] == 13);
        REQUIRE(test_array[4] == 14);
        REQUIRE(test_array[5] == 15);
        REQUIRE(test_array[6] == 16);
        REQUIRE(test_array[7] == 17);
        REQUIRE(test_array[8] == 18);
        REQUIRE(test_array[9] == 19);

        size_t value = 10;

        for (minstd::array<uint32_t, 10>::iterator itr = test_array.begin(); itr != test_array.end(); itr++, value++)
        {
            REQUIRE(*itr == value);
        }
    }

    TEST_CASE("Test array with class", "")
    {

        minstd::array<TestElement, 5> test_array;

        test_array[0] = TestElement(110);
        test_array[1] = TestElement(111);
        test_array[2] = TestElement(112);
        test_array[3] = TestElement(113);
        test_array[4] = TestElement(114);

        REQUIRE(test_array.front().value() == 110);
        REQUIRE(test_array.back().value() == 114);
        REQUIRE(test_array.size() == 5);
        REQUIRE(test_array.max_size() == 5);
        REQUIRE(!test_array.empty());

        REQUIRE(test_array[0].value() == 110);
        REQUIRE(test_array[1].value() == 111);
        REQUIRE(test_array[2].value() == 112);
        REQUIRE(test_array[3].value() == 113);
        REQUIRE(test_array[4].value() == 114);

        size_t value = 110;

        for (minstd::array<TestElement, 5>::iterator itr = test_array.begin(); itr != test_array.end(); itr++, value++)
        {
            REQUIRE(itr->value() == value);
        }
    }

    TEST_CASE("Test array fill with class", "")
    {

        minstd::array<TestElement, 5> test_array;

        test_array.fill(TestElement(200));

        test_array[0].value() += 0;
        test_array[1].value() += 1;
        test_array[2].value() += 2;
        test_array.at(3).value() += 3;
        test_array.at(4).value() += 4;

        REQUIRE(test_array.front().value() == 200);
        REQUIRE(test_array.back().value() == 204);
        REQUIRE(test_array.size() == 5);
        REQUIRE(test_array.max_size() == 5);
        REQUIRE(!test_array.empty());

        REQUIRE(test_array.at(0).value() == 200);
        REQUIRE(test_array.at(1).value() == 201);
        REQUIRE(test_array.at(2).value() == 202);
        REQUIRE(test_array.at(3).value() == 203);
        REQUIRE(test_array.at(4).value() == 204);

        size_t value = 200;

        for (minstd::array<TestElement, 5>::iterator itr = test_array.begin(); itr != test_array.end(); itr++, value++)
        {
            REQUIRE(itr->value() == value);
        }
    }
}
