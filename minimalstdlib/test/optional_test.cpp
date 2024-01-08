// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"

#include "../include/optional"

namespace
{

    class TestElement
    {
    public:
        TestElement()
            : value1_(0),
              value2_(0)
        {
        }

        explicit TestElement(uint32_t value1,
                             uint32_t value2)
            : value1_(value1),
              value2_(value2)
        {
        }

        TestElement(const TestElement &) = default;

        uint32_t value1() const
        {
            return value1_;
        }

        uint32_t &value1()
        {
            return value1_;
        }

        uint32_t value2() const
        {
            return value2_;
        }

        uint32_t &value2()
        {
            return value2_;
        }

    private:
        uint32_t value1_;
        uint32_t value2_;
    };

    TEST_CASE("Test with primitive", "")
    {
        minstd::optional<uint32_t> test_uint32_t1;

        REQUIRE(test_uint32_t1.has_value() == false);

        minstd::optional<uint32_t> test_uint32_t2(7);

        REQUIRE(test_uint32_t2.has_value() == true);
        REQUIRE(test_uint32_t2.value() == 7);
    }

    TEST_CASE("Test with object", "")
    {
        minstd::optional<TestElement> test_elem1;

        REQUIRE(test_elem1.has_value() == false);

        minstd::optional<TestElement> test_elem2( TestElement(9,10) );

        REQUIRE(test_elem2.has_value() == true);
        REQUIRE(test_elem2.value().value1() == 9);
        REQUIRE(test_elem2.value().value2() == 10);
        REQUIRE((*test_elem2).value1() == 9);
        REQUIRE((*test_elem2).value2() == 10);

        minstd::optional<TestElement> test_elem3( TestElement(11, 12) );

        REQUIRE(test_elem3.has_value() == true);
        REQUIRE(test_elem3.value().value1() == 11);
        REQUIRE(test_elem3.value().value2() == 12);
        REQUIRE(test_elem3->value1() == 11);
        REQUIRE(test_elem3->value2() == 12);
    }
}
