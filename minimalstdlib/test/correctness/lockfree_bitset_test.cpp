// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/bitset>

#include <atomic>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (BitsetTests)
    {
    };
#pragma GCC diagnostic pop

    TEST(BitsetTests, AcquireReleaseSingleBit)
    {
        minstd::bitset<128> semaphores;
        using result = minstd::bitset_result;

        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(7));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_ACQUIRED, semaphores.acquire(7));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(7));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_RELEASED, semaphores.release(7));

        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(8));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(8));

        CHECK_EQUAL(result::INDEX_OUT_OF_RANGE, semaphores.acquire(128));
        CHECK_EQUAL(result::INDEX_OUT_OF_RANGE, semaphores.release(128));
    }

    TEST(BitsetTests, ReleaseWithoutAcquire)
    {
        minstd::bitset<128> semaphores;
        using result = minstd::bitset_result;

        CHECK_EQUAL(result::FAILED_BIT_ALREADY_RELEASED, semaphores.release(0));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_RELEASED, semaphores.release(63));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_RELEASED, semaphores.release(64));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_RELEASED, semaphores.release(127));
    }

    TEST(BitsetTests, AcquireReleaseAcrossWordBoundaries)
    {
        minstd::bitset<256> semaphores;
        using result = minstd::bitset_result;

        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(0));
        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(63));
        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(64));
        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(127));

        CHECK_EQUAL(result::FAILED_BIT_ALREADY_ACQUIRED, semaphores.acquire(0));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_ACQUIRED, semaphores.acquire(64));

        CHECK_EQUAL(result::SUCCESS, semaphores.release(63));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(127));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(0));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(64));
    }

    TEST(BitsetTests, AcquireReleaseWithScaling)
    {
        minstd::bitset<512, 4> semaphores;
        using result = minstd::bitset_result;

        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(1));
        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(128));
        CHECK_EQUAL(result::SUCCESS, semaphores.acquire(255));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_ACQUIRED, semaphores.acquire(128));

        CHECK_EQUAL(result::SUCCESS, semaphores.release(1));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(128));
        CHECK_EQUAL(result::SUCCESS, semaphores.release(255));
        CHECK_EQUAL(result::FAILED_BIT_ALREADY_RELEASED, semaphores.release(255));
    }
}
