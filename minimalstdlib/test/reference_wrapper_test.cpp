// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"
#include "../include/minstd_utility.h"

#include "../include/functional"
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

    TEST_CASE("Test with class instance", "")
    {
        TestElement test_element(1, 2);

        minstd::reference_wrapper<TestElement> test_ref_wrapper(minstd::move(test_element));

        REQUIRE(test_ref_wrapper.get().value1() == 1);
        REQUIRE(test_ref_wrapper.get().value2() == 2);

        minstd::optional<minstd::reference_wrapper<TestElement>> test_optional_ref_wrapper(minstd::move(test_element));

        REQUIRE(test_optional_ref_wrapper.has_value());
        REQUIRE(test_optional_ref_wrapper->get().value1() == 1);
        REQUIRE(test_optional_ref_wrapper->get().value2() == 2);
    }
}
