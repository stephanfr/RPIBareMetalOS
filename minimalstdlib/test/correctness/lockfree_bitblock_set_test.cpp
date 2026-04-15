// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/bitblock_set>

#include <atomic>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static inline SimpleString StringFrom(minstd::bitblock_set_result value)
{
    return StringFrom(static_cast<int>(value));
}

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (BitblockSetTests)
    {
    };
#pragma GCC diagnostic pop

    using result = minstd::bitblock_set_result;

    TEST(BitblockSetTests, AcquireReleaseSingleBit)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(7));
        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(7));
        CHECK_EQUAL(result::success, blocks.release(7));
        CHECK_EQUAL(result::failed_bit_already_released, blocks.release(7));

        CHECK_EQUAL(result::success, blocks.acquire(8));
        CHECK_EQUAL(result::success, blocks.release(8));
    }

    TEST(BitblockSetTests, AcquireReleaseAcrossWordBoundaries)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(0));
        CHECK_EQUAL(result::success, blocks.acquire(63));
        CHECK_EQUAL(result::success, blocks.acquire(64));
        CHECK_EQUAL(result::success, blocks.acquire(127));
        CHECK_EQUAL(result::success, blocks.acquire(128));
        CHECK_EQUAL(result::success, blocks.acquire(255));

        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(0));
        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(64));
        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(128));

        CHECK_EQUAL(result::success, blocks.release(0));
        CHECK_EQUAL(result::success, blocks.release(63));
        CHECK_EQUAL(result::success, blocks.release(64));
        CHECK_EQUAL(result::success, blocks.release(127));
        CHECK_EQUAL(result::success, blocks.release(128));
        CHECK_EQUAL(result::success, blocks.release(255));
    }

    TEST(BitblockSetTests, IndexOutOfRange)
    {
        minstd::bitblock_set<128> blocks;

        CHECK_EQUAL(result::start_index_out_of_range, blocks.acquire(128));
        CHECK_EQUAL(result::start_index_out_of_range, blocks.acquire(1000));
        CHECK_EQUAL(result::start_index_out_of_range, blocks.release(128));
        CHECK_EQUAL(result::start_index_out_of_range, blocks.release(1000));
    }

    TEST(BitblockSetTests, IsAcquired)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_FALSE(blocks.is_acquired(10));
        blocks.acquire(10);
        CHECK_TRUE(blocks.is_acquired(10));
        blocks.release(10);
        CHECK_FALSE(blocks.is_acquired(10));

        CHECK_FALSE(blocks.is_acquired(256));
    }

    TEST(BitblockSetTests, TotalBitsSet)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(0u, blocks.total_bits_set());

        blocks.acquire(0);
        blocks.acquire(100);
        blocks.acquire(255);
        CHECK_EQUAL(3u, blocks.total_bits_set());

        blocks.release(100);
        CHECK_EQUAL(2u, blocks.total_bits_set());
    }

    TEST(BitblockSetTests, AcquireReleaseBlockSingleWord)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(10, 4));
        CHECK_TRUE(blocks.is_acquired(10));
        CHECK_TRUE(blocks.is_acquired(11));
        CHECK_TRUE(blocks.is_acquired(12));
        CHECK_TRUE(blocks.is_acquired(13));
        CHECK_FALSE(blocks.is_acquired(9));
        CHECK_FALSE(blocks.is_acquired(14));

        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(10, 4));
        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(11, 2));
        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(9, 2));

        CHECK_EQUAL(result::success, blocks.release(10, 4));
        CHECK_FALSE(blocks.is_acquired(10));
        CHECK_FALSE(blocks.is_acquired(11));
        CHECK_FALSE(blocks.is_acquired(12));
        CHECK_FALSE(blocks.is_acquired(13));

        CHECK_EQUAL(result::failed_bit_already_released, blocks.release(10, 4));
    }

    TEST(BitblockSetTests, AcquireReleaseBlockMultiWord)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(60, 8));
        CHECK_TRUE(blocks.is_acquired(60));
        CHECK_TRUE(blocks.is_acquired(63));
        CHECK_TRUE(blocks.is_acquired(64));
        CHECK_TRUE(blocks.is_acquired(67));
        CHECK_FALSE(blocks.is_acquired(59));
        CHECK_FALSE(blocks.is_acquired(68));

        CHECK_EQUAL(result::failed_bit_already_acquired_first_word, blocks.acquire(60, 8));
        CHECK_EQUAL(result::failed_bit_already_acquired_first_word, blocks.acquire(62, 4));
        CHECK_EQUAL(result::failed_bit_already_acquired, blocks.acquire(59, 3));

        CHECK_EQUAL(result::success, blocks.release(60, 8));
        CHECK_FALSE(blocks.is_acquired(60));
        CHECK_FALSE(blocks.is_acquired(63));
        CHECK_FALSE(blocks.is_acquired(64));
        CHECK_FALSE(blocks.is_acquired(67));
    }

    TEST(BitblockSetTests, AcquireBlockVariousSizes)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(0, 1));
        CHECK_EQUAL(result::success, blocks.acquire(10, 2));
        CHECK_EQUAL(result::success, blocks.acquire(20, 8));
        CHECK_EQUAL(result::success, blocks.acquire(30, 16));
        CHECK_EQUAL(result::success, blocks.acquire(60, 10));

        CHECK_EQUAL(1u + 2u + 8u + 16u + 10u, blocks.total_bits_set());

        CHECK_EQUAL(result::success, blocks.release(0, 1));
        CHECK_EQUAL(result::success, blocks.release(10, 2));
        CHECK_EQUAL(result::success, blocks.release(20, 8));
        CHECK_EQUAL(result::success, blocks.release(30, 16));
        CHECK_EQUAL(result::success, blocks.release(60, 10));

        CHECK_EQUAL(0u, blocks.total_bits_set());
    }

    TEST(BitblockSetTests, AcquireBlockInvalidParameters)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::invalid_num_bits, blocks.acquire(0, 0));
        CHECK_EQUAL(result::invalid_num_bits, blocks.acquire(0, 17));
        CHECK_EQUAL(result::range_exceeds_size, blocks.acquire(250, 10));
        CHECK_EQUAL(result::range_exceeds_size, blocks.acquire(255, 2));

        CHECK_EQUAL(result::invalid_num_bits, blocks.release(0, 0));
        CHECK_EQUAL(result::invalid_num_bits, blocks.release(0, 17));
        CHECK_EQUAL(result::range_exceeds_size, blocks.release(250, 10));
    }

    TEST(BitblockSetTests, ReleaseBlockPartiallyAcquired)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(10));
        CHECK_EQUAL(result::success, blocks.acquire(12));

        CHECK_EQUAL(result::failed_bit_already_released, blocks.release(10, 3));

        CHECK_EQUAL(result::success, blocks.release(10));
        CHECK_EQUAL(result::success, blocks.release(12));
    }

    TEST(BitblockSetTests, AcquireBlockAt16ByteBoundary)
    {
        minstd::bitblock_set<256> blocks;

        CHECK_EQUAL(result::success, blocks.acquire(0, 16));
        CHECK_EQUAL(16u, blocks.total_bits_set());

        CHECK_EQUAL(result::success, blocks.acquire(128, 16));
        CHECK_EQUAL(32u, blocks.total_bits_set());

        CHECK_EQUAL(result::success, blocks.release(0, 16));
        CHECK_EQUAL(result::success, blocks.release(128, 16));
        CHECK_EQUAL(0u, blocks.total_bits_set());
    }

    TEST(BitblockSetTests, AcquireFirstAvailableSequential)
    {
        minstd::bitblock_set<256> blocks;

        for (size_t i = 0; i < 256; ++i)
        {
            auto result = blocks.acquire_first_available();
            CHECK_TRUE(result.has_value());
            CHECK_EQUAL(i, result.value());
        }

        auto exhausted = blocks.acquire_first_available();
        CHECK_FALSE(exhausted.has_value());
    }

    TEST(BitblockSetTests, AcquireFirstAvailableExhaustion)
    {
        minstd::bitblock_set<128> blocks;

        for (size_t i = 0; i < 128; ++i)
        {
            auto result = blocks.acquire_first_available();
            CHECK_TRUE(result.has_value());
        }

        CHECK_FALSE(blocks.acquire_first_available().has_value());
        CHECK_FALSE(blocks.acquire_first_available().has_value());
    }

    TEST(BitblockSetTests, AcquireFirstAvailableWithGaps)
    {
        minstd::bitblock_set<256> blocks;

        for (size_t i = 0; i < 256; ++i)
        {
            blocks.acquire(i);
        }

        blocks.release(50);
        blocks.release(150);
        blocks.release(200);

        auto r1 = blocks.acquire_first_available();
        CHECK_TRUE(r1.has_value());
        CHECK_EQUAL(50u, r1.value());

        auto r2 = blocks.acquire_first_available();
        CHECK_TRUE(r2.has_value());
        CHECK_EQUAL(150u, r2.value());

        auto r3 = blocks.acquire_first_available();
        CHECK_TRUE(r3.has_value());
        CHECK_EQUAL(200u, r3.value());

        CHECK_FALSE(blocks.acquire_first_available().has_value());
    }

    TEST(BitblockSetTests, AcquireFirstAvailableNonMultipleOf64)
    {
        minstd::bitblock_set<200> blocks;

        CHECK_EQUAL(200u, blocks.size());

        for (size_t i = 0; i < 200; ++i)
        {
            auto result = blocks.acquire_first_available();
            CHECK_TRUE(result.has_value());
            CHECK_EQUAL(i, result.value());
        }

        CHECK_FALSE(blocks.acquire_first_available().has_value());

        CHECK_EQUAL(result::start_index_out_of_range, blocks.acquire(200));
        CHECK_EQUAL(result::start_index_out_of_range, blocks.release(200));
    }

    TEST(BitblockSetTests, AcquireFirstAvailableOddWords)
    {
        minstd::bitblock_set<192> blocks;

        for (size_t i = 0; i < 192; ++i)
        {
            auto result = blocks.acquire_first_available();
            CHECK_TRUE(result.has_value());
            CHECK_EQUAL(i, result.value());
        }

        CHECK_FALSE(blocks.acquire_first_available().has_value());
    }

    TEST(BitblockSetTests, AcquireFirstAvailableWraparound)
    {
        minstd::bitblock_set<256> blocks;

        for (size_t i = 0; i < 200; ++i)
        {
            blocks.acquire(i);
        }

        auto result = blocks.acquire_first_available();
        CHECK_TRUE(result.has_value());
        CHECK_EQUAL(200u, result.value());

        blocks.release(10);

        result = blocks.acquire_first_available();
        CHECK_TRUE(result.has_value());
        CHECK_TRUE(result.value() == 10 || result.value() > 200);
    }

    namespace
    {
        minstd::atomic<bool> start_flag{false};

        static constexpr size_t MT_SIZE = 8192;

        struct alignas(64) mt_thread_args
        {
            minstd::bitblock_set<MT_SIZE> *blocks_ = nullptr;
            size_t acquired_count_ = 0;
            bool any_duplicate_ = false;
        };

        void *mt_acquire_worker(void *arg)
        {
            auto *args = static_cast<mt_thread_args *>(arg);

            while (!start_flag.load(minstd::memory_order_acquire))
            {
            }

            while (true)
            {
                auto result = args->blocks_->acquire_first_available();

                if (!result.has_value())
                {
                    break;
                }

                args->acquired_count_++;
            }

            return nullptr;
        }
    }

    TEST(BitblockSetTests, MultiThreadedAcquireFirstAvailable)
    {
        constexpr size_t NUM_THREADS = 8;

        minstd::bitblock_set<MT_SIZE> blocks;
        pthread_t threads[NUM_THREADS];
        mt_thread_args arguments[NUM_THREADS];

        start_flag.store(false, minstd::memory_order_release);

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            arguments[i].blocks_ = &blocks;
            arguments[i].acquired_count_ = 0;
            pthread_create(&threads[i], nullptr, mt_acquire_worker, &arguments[i]);
        }

        start_flag.store(true, minstd::memory_order_release);

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            pthread_join(threads[i], nullptr);
        }

        size_t total_acquired = 0;

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            total_acquired += arguments[i].acquired_count_;
        }

        CHECK_EQUAL(MT_SIZE, total_acquired);
        CHECK_EQUAL(MT_SIZE, blocks.total_bits_set());
    }

    TEST(BitblockSetTests, OrphanBitBugMultiWordAcquireFirstWordConflict)
    {
        // This test demonstrates the orphan bit bug discovered by TLA+ analysis.
        // When a multi-word acquire fails on the first word, the fetch_or operation
        // has already set bits atomically. The current implementation does NOT roll
        // back these newly-set bits, creating "orphan bits" that are set but unowned.

        minstd::bitblock_set<256> blocks;

        // Step 1: Acquire bit 63 (last bit of word 0)
        CHECK_EQUAL(result::success, blocks.acquire(63));
        CHECK_TRUE(blocks.is_acquired(63));
        CHECK_EQUAL(1u, blocks.total_bits_set());

        // Step 2: Try to acquire bits 62-65 (spans words)
        // This range starts at bit 62 in word 0 and ends at bit 65 in word 1
        // - Bits 62-63 are in word 0 (2 bits)
        // - Bits 64-65 are in word 1 (2 bits)
        //
        // What SHOULD happen:
        //   1. fetch_or(word0, mask{62,63}) sees bit 63 already set → FAIL
        //   2. Roll back any newly set bits (bit 62)
        //   3. Return failure, leave NO orphan bits
        //
        // What ACTUALLY happens (BUG):
        //   1. fetch_or(word0, mask{62,63}) sets BOTH bits 62 and 63, returns old value showing bit 63 was set
        //   2. Code detects conflict (bit 63 was already set)
        //   3. Returns failure WITHOUT rolling back bit 62
        //   4. Bit 62 is now an ORPHAN (set but no owner can release it)

        auto result = blocks.acquire(62, 4);

        // The acquire SHOULD fail because bit 63 is already acquired
        CHECK_EQUAL(result::failed_bit_already_acquired_first_word, result);

        // BUG MANIFESTATION: Check for orphan bits
        // Bit 62 should NOT be set (we never successfully acquired it)
        // But due to the bug, fetch_or set it during the failed acquire
        printf("\nOrphan Bit Bug Test Results:\n");
        printf("  Bit 62 is_acquired: %s (should be false, bug makes it true)\n",
               blocks.is_acquired(62) ? "TRUE" : "FALSE");
        printf("  Bit 63 is_acquired: %s (should be true)\n",
               blocks.is_acquired(63) ? "TRUE" : "FALSE");
        printf("  Total bits set: %zu (should be 1, bug makes it 2)\n",
               blocks.total_bits_set());

        // This assertion will FAIL, demonstrating the bug
        CHECK_EQUAL(1u, blocks.total_bits_set()); // Expected: only bit 63
        CHECK_FALSE(blocks.is_acquired(62));      // Bit 62 should NOT be set

        // The orphan bit 62 is now stuck - trying to release it fails
        // because we never "acquired" it through the normal path
        CHECK_EQUAL(result::failed_bit_already_released, blocks.release(62));

        // But we can still release the properly acquired bit 63
        CHECK_EQUAL(result::success, blocks.release(63));
    }

    //  Test for release_multi_word TOCTOU race.
    //
    //  The bug: release_multi_word loads both words, checks that all bits are set,
    //  then does two separate fetch_and calls to clear them. Two threads releasing
    //  the SAME range concurrently can both pass the check and both execute the
    //  fetch_and, so release returns success to BOTH callers (double release).
    //
    //  This test acquires a cross-word range, then races N threads to release it.
    //  Exactly one should succeed; the rest should fail. If more than one succeeds,
    //  the TOCTOU bug is present.

    namespace
    {
        static constexpr size_t RACE_SIZE = 256;
        static constexpr size_t RACE_THREADS = 8;
        static constexpr size_t RACE_ITERATIONS = 10000;

        struct alignas(64) race_thread_args
        {
            minstd::bitblock_set<RACE_SIZE> *blocks_ = nullptr;
            minstd::atomic<size_t> *epoch_ = nullptr;
            minstd::atomic<size_t> *done_count_ = nullptr;
            size_t success_count_ = 0;
            size_t iterations_ = 0;
        };

        void *release_race_worker(void *arg)
        {
            auto *args = static_cast<race_thread_args *>(arg);

            for (size_t iter = 1; iter <= args->iterations_; ++iter)
            {
                // Wait for the main thread to advance the epoch to this iteration
                while (args->epoch_->load(minstd::memory_order_acquire) < iter)
                {
                }

                // Race to release the cross-word range (bits 60-67)
                auto result = args->blocks_->release(60, 8);

                if (result == minstd::bitblock_set_result::success)
                {
                    args->success_count_++;
                }

                // Signal done
                args->done_count_->fetch_add(1, minstd::memory_order_release);
            }

            return nullptr;
        }
    }

    TEST(BitblockSetTests, ReleaseMultiWordTOCTOURace)
    {
        minstd::bitblock_set<RACE_SIZE> blocks;
        minstd::atomic<size_t> epoch{0};
        minstd::atomic<size_t> done_count{0};

        pthread_t threads[RACE_THREADS];
        race_thread_args arguments[RACE_THREADS];

        for (size_t i = 0; i < RACE_THREADS; ++i)
        {
            arguments[i].blocks_ = &blocks;
            arguments[i].epoch_ = &epoch;
            arguments[i].done_count_ = &done_count;
            arguments[i].success_count_ = 0;
            arguments[i].iterations_ = RACE_ITERATIONS;
            pthread_create(&threads[i], nullptr, release_race_worker, &arguments[i]);
        }

        size_t total_double_releases = 0;

        for (size_t iter = 1; iter <= RACE_ITERATIONS; ++iter)
        {
            // Acquire the cross-word range
            blocks.acquire(60, 8);

            // Reset done counter and release the threads for this epoch
            done_count.store(0, minstd::memory_order_relaxed);
            epoch.store(iter, minstd::memory_order_release);

            // Wait for all threads to complete their attempt
            while (done_count.load(minstd::memory_order_acquire) < RACE_THREADS)
            {
            }
        }

        for (size_t i = 0; i < RACE_THREADS; ++i)
        {
            pthread_join(threads[i], nullptr);
        }

        // Count total successes across all threads
        size_t total_successes = 0;
        for (size_t i = 0; i < RACE_THREADS; ++i)
        {
            total_successes += arguments[i].success_count_;
        }

        // Exactly one thread should succeed per iteration
        total_double_releases = total_successes - RACE_ITERATIONS;

        printf("\nRelease Multi-Word TOCTOU Race Results:\n");
        printf("  Iterations: %zu\n", RACE_ITERATIONS);
        printf("  Total successes: %zu (expected %zu)\n", total_successes, RACE_ITERATIONS);
        printf("  Double releases detected: %zu\n", total_double_releases);

        // If the TOCTOU bug exists, total_successes > RACE_ITERATIONS
        CHECK_EQUAL(RACE_ITERATIONS, total_successes);
    }
}
