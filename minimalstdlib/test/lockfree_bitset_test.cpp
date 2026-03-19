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

    namespace
    {
        minstd::atomic<bool> start_updates{false};

        template <size_t CACHE_LINE_SCALING>
        struct alignas(64) perf_thread_arguments
        {
            minstd::bitset<16384, CACHE_LINE_SCALING> *semaphores_ = nullptr;
            uint64_t rng_state_ = 0;
            size_t iterations_ = 0;
        };

        uint64_t xorshift64(uint64_t &state)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        }

        template <size_t CACHE_LINE_SCALING>
        void *perf_worker(void *arguments)
        {
            auto *args = static_cast<perf_thread_arguments<CACHE_LINE_SCALING> *>(arguments);

            while (!start_updates.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                size_t index = static_cast<size_t>(xorshift64(args->rng_state_) % 16384);
                auto result = args->semaphores_->acquire(index);

                if (result == minstd::bitset_result::SUCCESS)
                {
                    args->semaphores_->release(index);
                }
            }

            return nullptr;
        }

        template <size_t CACHE_LINE_SCALING>
        void run_scaling_test(const char *label, double *ops_per_sec_out, const double *baseline_ops_per_sec)
        {
            constexpr size_t SIZE_IN_BITS = 16384;
            constexpr size_t MAX_THREADS = 32;
            constexpr size_t ITERATIONS_PER_THREAD = 5000000;

            minstd::bitset<SIZE_IN_BITS, CACHE_LINE_SCALING> semaphores;

            pthread_t threads[MAX_THREADS];
            perf_thread_arguments<CACHE_LINE_SCALING> arguments[MAX_THREADS];

            printf("%s acquire/release ops/sec:\n", label);

            for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
            {
                start_updates.store(false, minstd::memory_order_release);

                for (size_t i = 0; i < num_threads; ++i)
                {
                    arguments[i].semaphores_ = &semaphores;
                    arguments[i].rng_state_ = 0x9e3779b97f4a7c15ULL ^ (i * 0x100000001b3ULL);
                    arguments[i].iterations_ = ITERATIONS_PER_THREAD;

                    pthread_create(&threads[i], nullptr, perf_worker<CACHE_LINE_SCALING>, &arguments[i]);
                }

                timespec start_time{};
                timespec end_time{};
                clock_gettime(CLOCK_MONOTONIC, &start_time);
                start_updates.store(true, minstd::memory_order_release);

                for (size_t i = 0; i < num_threads; ++i)
                {
                    pthread_join(threads[i], nullptr);
                }

                clock_gettime(CLOCK_MONOTONIC, &end_time);
                double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec);
                duration += static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9;
                double ops_per_sec = static_cast<double>(ITERATIONS_PER_THREAD * num_threads) / duration;

                ops_per_sec_out[num_threads - 1] = ops_per_sec;

                if (baseline_ops_per_sec)
                {
                    const double ratio = ops_per_sec / baseline_ops_per_sec[num_threads - 1];
                    const double delta = (ratio - 1.0) * 100.0;
                    printf("  %2zu threads: %f (%+.2f%% vs scale=1)\n", num_threads, ops_per_sec, delta);
                }
                else
                {
                    printf("  %2zu threads: %f\n", num_threads, ops_per_sec);
                }
            }
        }
    }

    TEST(BitsetTests, AcquireReleaseThreadScaling)
    {
        constexpr size_t MAX_THREADS = 32;
        double baseline_ops_per_sec[MAX_THREADS] = {};
        double scale2_ops_per_sec[MAX_THREADS] = {};
        double scale4_ops_per_sec[MAX_THREADS] = {};

        run_scaling_test<1>("Bitset scale=1", baseline_ops_per_sec, nullptr);
        run_scaling_test<2>("Bitset scale=2", scale2_ops_per_sec, baseline_ops_per_sec);
        run_scaling_test<4>("Bitset scale=4", scale4_ops_per_sec, baseline_ops_per_sec);
    }
}
