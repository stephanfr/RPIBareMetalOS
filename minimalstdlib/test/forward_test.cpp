// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Adapted from cppreference.com example for std::forward
//      https://en.cppreference.com/w/cpp/utility/forward

<<<<<<< HEAD
#include <catch2/catch_test_macros.hpp>

#include "../include/minstd_utility.h"

#include "memory.h"
=======
#include <CppUTest/TestHarness.h>

#include <new>
#include <utility>
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

namespace
{

<<<<<<< HEAD
    char buf1[1024];
    char buf2[1024];
=======
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ForwardTests)
    {};
#pragma GCC diagnostic pop

    char buf1[1024] __attribute__((aligned(4)));
    char buf2[1024] __attribute__((aligned(4)));
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    struct A
    {
        A(int &&n) { value = n; } //  rvalue overload
        A(int &n) { value = n; }  //  lvalue overload

        int value;
    };

    class B
    {
    public:
        template <class T1, class T2, class T3>
        B(T1 &&t1, T2 &&t2, T3 &&t3) : a1_{minstd::forward<T1>(t1)},
                                       a2_{minstd::forward<T2>(t2)},
                                       a3_{minstd::forward<T3>(t3)}
        {
        }

        const A get_a1() const
        {
            return a1_;
        }

        const A get_a2() const
        {
            return a2_;
        }

        const A get_a3() const
        {
            return a3_;
        }

    private:
        A a1_, a2_, a3_;
    };

    template <class T, class U>
    T *create1(U &&u)
    {
<<<<<<< HEAD
        return new (buf1) T(std::forward<U>(u));
=======
        return new (buf1) T(minstd::forward<U>(u));
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    template <class T, class... U>
    T *create2(U &&...u)
    {
<<<<<<< HEAD
        return new (buf2) T(std::forward<U>(u)...);
=======
        return new (buf2) T(minstd::forward<U>(u)...);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    template <class T, class... U>
    T *create2_fails_at_compile_time(U &...u) //  Attempts to forward an lvalue ref as an rvalue - fails at compile time.
    {
<<<<<<< HEAD
        return new (buf2) T(std::forward<U>(u)...);
=======
        return new (buf2) T(minstd::forward<U>(u)...);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    auto make_B(auto &&...args) // since C++20
    {
<<<<<<< HEAD
        return B(std::forward<decltype(args)>(args)...);
    }

    TEST_CASE("Test minstd::forward<>()", "")
    {
        auto p1 = create1<A>(2); // rvalue

        REQUIRE(p1->value == 2);
=======
        return B(minstd::forward<decltype(args)>(args)...);
    }

    TEST(ForwardTests, BasicTest)
    {
        auto p1 = create1<A>(2); // rvalue

        CHECK(p1->value == 2);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        int i = 1;
        auto p2 = create1<A>(i); // lvalue

<<<<<<< HEAD
        REQUIRE(p2->value == i);

        auto t = create2<B>(2, i, 3);

        REQUIRE(t->get_a1().value == 2);
        REQUIRE(t->get_a2().value == 1);
        REQUIRE(t->get_a3().value == 3);

        B b = make_B(4, i, 5);

        REQUIRE(b.get_a1().value == 4);
        REQUIRE(b.get_a2().value == 1);
        REQUIRE(b.get_a3().value == 5);
=======
        CHECK(p2->value == i);

        auto t = create2<B>(2, i, 3);

        CHECK(t->get_a1().value == 2);
        CHECK(t->get_a2().value == 1);
        CHECK(t->get_a3().value == 3);

        B b = make_B(4, i, 5);

        CHECK(b.get_a1().value == 4);
        CHECK(b.get_a2().value == 1);
        CHECK(b.get_a3().value == 5);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //
        //  Uncomment below to insure the compiler fails to compile an rvalue as an lvalue
        //

        //    auto t2 = create2_fails_at_compile_time<B>(2, i, 3);
    }
}