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

    TEST(PairTests, PrimitivesTests)
    {
        minstd::pair<uint32_t, double> pair1;

        minstd::pair<uint32_t, double> pair2(22, 23.456);
        CHECK_EQUAL(pair2.first(), 22);
        CHECK_EQUAL(pair2.second(), 23.456);

        pair1 = pair2;
        CHECK_EQUAL(pair1.first(), 22);
        CHECK_EQUAL(pair1.second(), 23.456);

        minstd::pair<uint32_t, double> pair3(pair1);
        CHECK_EQUAL(pair3.first(), 22);
        CHECK_EQUAL(pair3.second(), 23.456);

        const minstd::pair<uint32_t, double> pair4(pair3);
        CHECK_EQUAL(pair4.first(), 22);
        CHECK_EQUAL(pair4.second(), 23.456);
    }

    TEST(PairTests, ObjectTests)
    {
        minstd::pair<minstd::fixed_string<>, TestElement> pair1;

        minstd::pair<minstd::fixed_string<>, TestElement> pair2("string is first", TestElement(51));
        STRCMP_EQUAL(pair2.first(), "string is first");
        CHECK_EQUAL(pair2.second().value(), 51);

        pair1 = pair2;
        STRCMP_EQUAL(pair1.first(), "string is first");
        CHECK_EQUAL(pair1.second().value(), 51);

        minstd::pair<minstd::fixed_string<>, TestElement> pair3(pair1);
        STRCMP_EQUAL(pair3.first(), "string is first");
        CHECK_EQUAL(pair3.second().value(), 51);

        const minstd::pair<minstd::fixed_string<>, TestElement> pair4(pair3);
        STRCMP_EQUAL(pair4.first(), "string is first");
        CHECK_EQUAL(pair4.second().value(), 51);
    }
}
