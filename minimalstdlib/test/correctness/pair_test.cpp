// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <stdint.h>

#include <minstdconfig.h>

#include <fixed_string>
#include <utility>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(PairTests){};
#pragma GCC diagnostic pop

    class test_element
    {
    public:
        test_element()
            : value_(0)
        {
        }

        explicit test_element(uint32_t value)
            : value_(value)
        {
        }

        test_element(const test_element &) = default;

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

    TEST(PairTests, PrimitivesTests)
    {
        minstd::pair<uint32_t, double> pair1;

        minstd::pair<uint32_t, double> pair2(22, 23.456);
        CHECK_EQUAL(get<0>(pair2), 22);
        CHECK_EQUAL(get<1>(pair2), 23.456);

        pair1 = pair2;
        CHECK_EQUAL(get<0>(pair1), 22);
        CHECK_EQUAL(get<1>(pair1), 23.456);

        minstd::pair<uint32_t, double> pair3(pair1);
        CHECK_EQUAL(get<0>(pair3), 22);
        CHECK_EQUAL(get<1>(pair3), 23.456);

        const minstd::pair<uint32_t, double> pair4(pair3);
        CHECK_EQUAL(get<0>(pair4), 22);
        CHECK_EQUAL(get<1>(pair4), 23.456);
    }

    TEST(PairTests, ObjectTests)
    {
        minstd::pair<minstd::fixed_string<>, test_element> pair1;

        minstd::pair<minstd::fixed_string<>, test_element> pair2("string is first", test_element(51));
        STRCMP_EQUAL(get<0>(pair2), "string is first");
        CHECK_EQUAL(get<1>(pair2).value(), 51);

        pair1 = pair2;
        STRCMP_EQUAL(get<0>(pair1), "string is first");
        CHECK_EQUAL(get<1>(pair1).value(), 51);

        minstd::pair<minstd::fixed_string<>, test_element> pair3(pair1);
        STRCMP_EQUAL(get<0>(pair3), "string is first");
        CHECK_EQUAL(get<1>(pair3).value(), 51);

        const minstd::pair<minstd::fixed_string<>, test_element> pair4(pair3);
        STRCMP_EQUAL(get<0>(pair4), "string is first");
        CHECK_EQUAL(get<1>(pair4).value(), 51);
    }
}
