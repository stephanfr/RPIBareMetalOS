// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <tuple>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (TupleTests)
    {
    };
#pragma GCC diagnostic pop
/*
    TEST(TupleTests, BasicCases)
    {
        using tuplet::tuple;

        auto tup = tuple{
            std::make_unique<int>(10),
            std::make_unique<int>(20),
            std::make_unique<int>(30)};

        auto is_valid_ref = [](auto &value)
        { return bool(value); };
        auto is_valid_move = [](auto value)
        { return bool(value); };

        CHECK(tup.all(is_valid_ref));
        CHECK(minstd::move(tup).all(is_valid_move));
        CHECK(!tup.all(is_valid_ref));
    }
    */
}