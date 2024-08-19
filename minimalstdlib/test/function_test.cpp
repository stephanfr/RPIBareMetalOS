// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <functional>

#include <fixed_string>

#include <new>

//
//  Adapted from cplusplus.com reference for std::function
//

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(FunctionTests)
    {};
#pragma GCC diagnostic pop

    // a function:
    int half(int x) { return x / 2; }

    // a function object class:
    struct third_t
    {
        int operator()(int x) { return x / 3; }
    };

    // a class with data members:
    struct MyValue
    {
        int value;
        int fifth() { return value / 5; }
    };

    template <class _Ty = void>
    struct negate
    {
        constexpr _Ty operator()(const _Ty &_Left) const
        {
            return -_Left;
        }
    };

    TEST(FunctionTests, BasicTests)
    {
        minstd::function<int(int)> fn1 = half;      // function
        minstd::function<int(int)> fn2 = &half;     // function pointer
        minstd::function<int(int)> fn3 = third_t(); // function object
        minstd::function<int(int)> fn4 = [](int x) { return x / 4; };   // lambda expression
        minstd::function<int(int)> fn5 = negate<int>(); // standard function object

        CHECK_EQUAL( fn1(60), 30 );
        CHECK_EQUAL( fn2(60), 30 );
        CHECK_EQUAL( fn3(60), 20 );
        CHECK_EQUAL( fn4(60), 15 );
        CHECK_EQUAL( fn5(60), -60 );

        //  Class instance with members:
        minstd::function<int(MyValue &)> value = [](MyValue &value){ return value.value; }; // lambda to return data member
        minstd::function<int(MyValue &)> fifth = [](MyValue &value){ return value.fifth(); }; // lambda to call member function

        MyValue sixty{60};

        CHECK_EQUAL( value(sixty), 60 );
        CHECK_EQUAL( fifth(sixty), 12 );
    }
}