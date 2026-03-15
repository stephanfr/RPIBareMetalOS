// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Adapted from cppreference.com example for std::forward
//      https://en.cppreference.com/w/cpp/utility/forward

#include <CppUTest/TestHarness.h>

#include <new>
#include <utility>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ForwardTests)
    {};
#pragma GCC diagnostic pop

    char buf1[1024] __attribute__((aligned(4)));
    char buf2[1024] __attribute__((aligned(4)));

    struct a
    {
        a(int &&n) { value = n; } //  rvalue overload
        a(int &n) { value = n; }  //  lvalue overload

        int value;
    };

    class b
    {
    public:
        template <class T1, class T2, class T3>
        b(T1 &&t1, T2 &&t2, T3 &&t3) : a1_{minstd::forward<T1>(t1)},
                                       a2_{minstd::forward<T2>(t2)},
                                       a3_{minstd::forward<T3>(t3)}
        {
        }

        const a get_a1() const
        {
            return a1_;
        }

        const a get_a2() const
        {
            return a2_;
        }

        const a get_a3() const
        {
            return a3_;
        }

    private:
        a a1_, a2_, a3_;
    };

    template <class T, class U>
    T *create1(U &&u)
    {
        return new (buf1) T(minstd::forward<U>(u));
    }

    template <class T, class... U>
    T *create2(U &&...u)
    {
        return new (buf2) T(minstd::forward<U>(u)...);
    }

    template <class T, class... U>
    T *create2_fails_at_compile_time(U &...u) //  Attempts to forward an lvalue ref as an rvalue - fails at compile time.
    {
        return new (buf2) T(minstd::forward<U>(u)...);
    }

    auto make_B(auto &&...args) // since C++20
    {
        return b(minstd::forward<decltype(args)>(args)...);
    }

    TEST(ForwardTests, BasicTest)
    {
        auto p1 = create1<a>(2); // rvalue

        CHECK(p1->value == 2);

        int i = 1;
        auto p2 = create1<a>(i); // lvalue

        CHECK(p2->value == i);

        auto t = create2<b>(2, i, 3);

        CHECK(t->get_a1().value == 2);
        CHECK(t->get_a2().value == 1);
        CHECK(t->get_a3().value == 3);

        b b = make_B(4, i, 5);

        CHECK(b.get_a1().value == 4);
        CHECK(b.get_a2().value == 1);
        CHECK(b.get_a3().value == 5);

        //
        //  Uncomment below to insure the compiler fails to compile an rvalue as an lvalue
        //

        //    auto t2 = create2_fails_at_compile_time<b>(2, i, 3);
    }
}