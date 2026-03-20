// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <array>
#include <lockfree/token_dispenser>

#include <pthread.h>

#include <stdio.h>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (LockfreeTokenDispenserTests)
    {
    };
#pragma GCC diagnostic pop

    struct args
    {
        minstd::token_dispenser<64> *dispenser;
        minstd::array<uint32_t, 1020> *tokens;
    };

    void *get_tokens(void *arguments)
    {
        minstd::token_dispenser<64> &dispenser = *static_cast<args *>(arguments)->dispenser;
        minstd::array<uint32_t, 1020> &tokens = *static_cast<args *>(arguments)->tokens;

        for (int i = 0; i < 1020; i++)
        {
            minstd::token_dispenser<64>::token token = dispenser.get_token();

            tokens[i] = token.value();
        }

        return nullptr;
    }

    TEST(LockfreeTokenDispenserTests, BasicTest)
    {
        minstd::token_dispenser test_dispenser(16);

        for (size_t i = 0; i < 100; i++)
        {
            CHECK_EQUAL(i % (uint32_t)16, test_dispenser.get_token().value());
        }
    }

    TEST(LockfreeTokenDispenserTests, MultithreadedTest)
    {
#ifdef __SANITIZE_THREAD__
        static constexpr size_t NUM_THREADS = 50;
#else
        static constexpr size_t NUM_THREADS = 1000;
#endif

        minstd::token_dispenser test_dispenser(17);

        minstd::array<uint32_t, 1020> tokens[1000];

        pthread_t test_threads[NUM_THREADS];

        args thread_args[NUM_THREADS];

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            thread_args[i].dispenser = &test_dispenser;
            thread_args[i].tokens = &tokens[i];

            CHECK(pthread_create(&test_threads[i], nullptr, get_tokens, &thread_args[i]) == 0);
        }

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(test_threads[i], nullptr);
        }

        minstd::array<uint32_t, 17> aggregated_values;

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            for (uint32_t j = 0; j < 1020; j++)
            {
                aggregated_values[tokens[i][j]]++;
            }
        }

        uint32_t total = 0;

        for (uint32_t i = 0; i < 17; i++)
        {
            total += aggregated_values[i];
        }

        //  The total should be 1000 * 1020.
        //      There is no guarantee that each token will have the same count, but the total should always be the same.

        CHECK_EQUAL(NUM_THREADS * 1020, total);
    }
}
