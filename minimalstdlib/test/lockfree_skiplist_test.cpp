// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/skiplist>

#include <pthread.h>


namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistTests)
    {
    };
#pragma GCC diagnostic pop
/*
    TEST(SkiplistTests, BasicFunctionality)
    {
        minstd::SkipList<uint32_t, uint32_t> list;

        for (uint32_t i = 0; i < 100; i++)
        {
            list.insert(i, i);
        }

        CHECK(list.size() == 100);

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK(list.find(i) != nullptr);
            CHECK(*list.find(i) == i);
        }

        CHECK(list.find(101) == nullptr);

        for (uint32_t i = 0; i < 10; i++)
        {
            list.remove(i * 10);
            list.remove((i * 10) + 1);
        }

        CHECK(list.size() == 80);

        for (uint32_t i = 0; i < 100; i++)
        {
            if ((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0)))
            {
                CHECK(list.find(i) == nullptr);
            }
            else
            {
                CHECK(list.find(i) != nullptr);
                CHECK(*list.find(i) == i);
            }
        }

        //  Cannot insert the same key twice

        CHECK(!list.insert(14, 114));
        CHECK(*list.find(14) == 14);

        CHECK(!list.insert(37, 137));
        CHECK(*list.find(37) == 37);

        //  Cannot insert the same key twice - even if that key is currently soft deleted

        CHECK(!list.insert(10, 10));
        CHECK(!list.insert(31, 31));
        CHECK(!list.insert(71, 71));

        //  Key 100 can be inserted b/c it was never in the list

        CHECK(list.insert(100, 100));

        CHECK(list.size() == 81);

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK(list.find(i) == nullptr);
            }
            else
            {
                CHECK(list.find(i) != nullptr);
                CHECK(*list.find(i) == i);
            }
        }

        CHECK_EQUAL(20, list.gc());

        CHECK_EQUAL(0, list.gc());

        CHECK(list.size() == 81);

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK(list.find(i) == nullptr);
            }
            else
            {
                CHECK(list.find(i) != nullptr);
                CHECK(*list.find(i) == i);
            }
        }

        //  Post-gc we can insert now

        CHECK(list.insert(10, 10));
        CHECK(list.insert(31, 31));
        CHECK(list.insert(71, 71));

        CHECK(list.size() == 84);

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) &&
                (i != 10) && (i != 31) && (i != 71) && (i != 100))
            {
                CHECK(list.find(i) == nullptr);
            }
            else
            {
                CHECK(list.find(i) != nullptr);
                CHECK(*list.find(i) == i);
            }
        }
    }
    */
}