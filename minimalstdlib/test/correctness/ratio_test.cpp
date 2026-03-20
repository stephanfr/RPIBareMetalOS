// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#include <CppUTest/TestHarness.h>

#include <ratio>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RatioTests){};
#pragma GCC diagnostic pop


    TEST(RatioTests, BasicTests)
    {
        typedef minstd::ratio<1,3> one_third;
        typedef minstd::ratio<2,4> two_fourths;

        CHECK_EQUAL(one_third::num, 1);
        CHECK_EQUAL(one_third::den, 3);

        CHECK_EQUAL(two_fourths::num, 1);
        CHECK_EQUAL(two_fourths::den, 2);

        typedef minstd::ratio_add<one_third, two_fourths> sum;

        CHECK_EQUAL(sum::num, 5);
        CHECK_EQUAL(sum::den, 6);

        typedef minstd::ratio_subtract<one_third, two_fourths> difference;

        CHECK_EQUAL(difference::num, -1);
        CHECK_EQUAL(difference::den, 6);

        typedef minstd::ratio_multiply<one_third, two_fourths> product;

        CHECK_EQUAL(product::num, 1);
        CHECK_EQUAL(product::den, 6);

        typedef minstd::ratio_divide<one_third, two_fourths> quotient;

        CHECK_EQUAL(quotient::num, 2);
        CHECK_EQUAL(quotient::den, 3);

        constexpr bool eq = minstd::ratio_equal<one_third, one_third>::value;
        CHECK_TRUE(eq);

        constexpr bool neq = minstd::ratio_not_equal<one_third, two_fourths>::value;
        CHECK_TRUE(neq);

        constexpr bool less = minstd::ratio_less<one_third, two_fourths>::value;
        CHECK_TRUE(less);

        constexpr bool leq = minstd::ratio_less_equal<one_third, two_fourths>::value;
        CHECK_TRUE(leq);

        constexpr bool leq2 = minstd::ratio_less_equal<two_fourths, two_fourths>::value;
        CHECK_TRUE(leq2);

        constexpr bool greater = minstd::ratio_greater<one_third, two_fourths>::value;
        CHECK_FALSE(greater);

        constexpr bool greater2 = minstd::ratio_greater<two_fourths, one_third>::value;
        CHECK_TRUE(greater2);

        constexpr bool geq = minstd::ratio_greater_equal<one_third, two_fourths>::value;
        CHECK_FALSE(geq);

        constexpr bool geq2 = minstd::ratio_greater_equal<two_fourths, one_third>::value;
        CHECK_TRUE(geq2);
    }
}