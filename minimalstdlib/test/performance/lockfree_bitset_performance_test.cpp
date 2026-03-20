// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/bitset>

#include "../shared/perf_test_config.h"
#include "../shared/perf_report.h"

#include <atomic>
#include <array>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (BitsetPerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    namespace
    {
        minstd::atomic<bool> start_updates{false};

        constexpr size_t SIZE_IN_BITS = 16384;
        constexpr size_t WORD_BITS = 64;
        constexpr size_t WORD_COUNT = SIZE_IN_BITS / WORD_BITS;
        constexpr size_t STRIPES = 32;

        struct mutex_bitset_global
        {
            uint64_t words_[WORD_COUNT] = {};
            pthread_mutex_t mutex_{};

            mutex_bitset_global()
            {
                pthread_mutex_init(&mutex_, nullptr);
            }

            ~mutex_bitset_global()
            {
                pthread_mutex_destroy(&mutex_);
            }

            void reset()
            {
                for (size_t i = 0; i < WORD_COUNT; ++i)
                {
                    words_[i] = 0;
                }
            }

            bool acquire(size_t index)
            {
                const size_t word = index / WORD_BITS;
                const uint64_t bit = 1ULL << (index % WORD_BITS);

                pthread_mutex_lock(&mutex_);
                const bool available = (words_[word] & bit) == 0;
                if (available)
                {
                    words_[word] |= bit;
                }
                pthread_mutex_unlock(&mutex_);

                return available;
            }

            void release(size_t index)
            {
                const size_t word = index / WORD_BITS;
                const uint64_t bit = 1ULL << (index % WORD_BITS);

                pthread_mutex_lock(&mutex_);
                words_[word] &= ~bit;
                pthread_mutex_unlock(&mutex_);
            }
        };

        struct mutex_bitset_striped
        {
            uint64_t words_[WORD_COUNT] = {};
            pthread_mutex_t stripe_mutex_[STRIPES]{};

            mutex_bitset_striped()
            {
                for (size_t i = 0; i < STRIPES; ++i)
                {
                    pthread_mutex_init(&stripe_mutex_[i], nullptr);
                }
            }

            ~mutex_bitset_striped()
            {
                for (size_t i = 0; i < STRIPES; ++i)
                {
                    pthread_mutex_destroy(&stripe_mutex_[i]);
                }
            }

            void reset()
            {
                for (size_t i = 0; i < WORD_COUNT; ++i)
                {
                    words_[i] = 0;
                }
            }

            bool acquire(size_t index)
            {
                const size_t word = index / WORD_BITS;
                const size_t stripe = word % STRIPES;
                const uint64_t bit = 1ULL << (index % WORD_BITS);

                pthread_mutex_lock(&stripe_mutex_[stripe]);
                const bool available = (words_[word] & bit) == 0;
                if (available)
                {
                    words_[word] |= bit;
                }
                pthread_mutex_unlock(&stripe_mutex_[stripe]);

                return available;
            }

            void release(size_t index)
            {
                const size_t word = index / WORD_BITS;
                const size_t stripe = word % STRIPES;
                const uint64_t bit = 1ULL << (index % WORD_BITS);

                pthread_mutex_lock(&stripe_mutex_[stripe]);
                words_[word] &= ~bit;
                pthread_mutex_unlock(&stripe_mutex_[stripe]);
            }
        };

        template <size_t CACHE_LINE_SCALING>
        struct alignas(64) perf_thread_arguments
        {
            minstd::bitset<SIZE_IN_BITS, CACHE_LINE_SCALING> *semaphores_ = nullptr;
            mutex_bitset_global *global_mutex_bitset_ = nullptr;
            mutex_bitset_striped *striped_mutex_bitset_ = nullptr;
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
        void *perf_worker_lockfree(void *arguments)
        {
            auto *args = static_cast<perf_thread_arguments<CACHE_LINE_SCALING> *>(arguments);

            while (!start_updates.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                size_t index = static_cast<size_t>(xorshift64(args->rng_state_) % SIZE_IN_BITS);
                auto result = args->semaphores_->acquire(index);

                if (result == minstd::bitset_result::SUCCESS)
                {
                    args->semaphores_->release(index);
                }
            }

            return nullptr;
        }

        template <size_t CACHE_LINE_SCALING>
        void *perf_worker_mutex_global(void *arguments)
        {
            auto *args = static_cast<perf_thread_arguments<CACHE_LINE_SCALING> *>(arguments);

            while (!start_updates.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                size_t index = static_cast<size_t>(xorshift64(args->rng_state_) % SIZE_IN_BITS);
                if (args->global_mutex_bitset_->acquire(index))
                {
                    args->global_mutex_bitset_->release(index);
                }
            }

            return nullptr;
        }

        template <size_t CACHE_LINE_SCALING>
        void *perf_worker_mutex_striped(void *arguments)
        {
            auto *args = static_cast<perf_thread_arguments<CACHE_LINE_SCALING> *>(arguments);

            while (!start_updates.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                size_t index = static_cast<size_t>(xorshift64(args->rng_state_) % SIZE_IN_BITS);
                if (args->striped_mutex_bitset_->acquire(index))
                {
                    args->striped_mutex_bitset_->release(index);
                }
            }

            return nullptr;
        }

        template <size_t CACHE_LINE_SCALING>
        double run_once(size_t num_threads,
                        size_t iterations_per_thread,
                        perf_thread_arguments<CACHE_LINE_SCALING> *arguments,
                        pthread_t *threads,
                        void *(*worker)(void *),
                        uint64_t base_seed)
        {
            start_updates.store(false, minstd::memory_order_release);

            for (size_t i = 0; i < num_threads; ++i)
            {
                arguments[i].rng_state_ = base_seed ^ (i * 0x100000001b3ULL);
                arguments[i].iterations_ = iterations_per_thread;
                pthread_create(&threads[i], nullptr, worker, &arguments[i]);
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
            return static_cast<double>(iterations_per_thread * num_threads) / duration;
        }

        template <size_t CACHE_LINE_SCALING>
        void run_scaling_test(const char *series_label,
                              const char *display_label,
                              double *ops_per_sec_out,
                              const double *baseline_ops_per_sec,
                              perf_report &report,
                              void *(*worker)(void *),
                              minstd::bitset<SIZE_IN_BITS, CACHE_LINE_SCALING> *lockfree_bitset,
                              mutex_bitset_global *global_mutex_bitset,
                              mutex_bitset_striped *striped_mutex_bitset,
                              uint64_t base_seed,
                              size_t max_threads,
                              size_t iterations_per_thread)
        {
            constexpr size_t MAX_THREADS = 16;
            const size_t WARMUP_ITERATIONS_PER_THREAD = (iterations_per_thread / 100 > 1000) ? (iterations_per_thread / 100) : 1000;

            pthread_t threads[MAX_THREADS];
            perf_thread_arguments<CACHE_LINE_SCALING> arguments[MAX_THREADS];

            for (size_t i = 0; i < MAX_THREADS; ++i)
            {
                arguments[i].semaphores_ = lockfree_bitset;
                arguments[i].global_mutex_bitset_ = global_mutex_bitset;
                arguments[i].striped_mutex_bitset_ = striped_mutex_bitset;
            }

            printf("%s acquire/release ops/sec:\n", display_label);

            for (size_t num_threads = 1; num_threads <= max_threads; ++num_threads)
            {
                if (global_mutex_bitset != nullptr)
                {
                    global_mutex_bitset->reset();
                }
                if (striped_mutex_bitset != nullptr)
                {
                    striped_mutex_bitset->reset();
                }

                run_once<CACHE_LINE_SCALING>(num_threads, WARMUP_ITERATIONS_PER_THREAD, arguments, threads, worker, base_seed ^ 0xa5a5a5a5ULL);
                double ops_per_sec = run_once<CACHE_LINE_SCALING>(num_threads, iterations_per_thread, arguments, threads, worker, base_seed);

                ops_per_sec_out[num_threads - 1] = ops_per_sec;

                char row_label[64];
                snprintf(row_label, sizeof(row_label), "%s %zu threads", series_label, num_threads);
                report.record(row_label, ops_per_sec, num_threads, iterations_per_thread);

                if (baseline_ops_per_sec)
                {
                    const double ratio = ops_per_sec / baseline_ops_per_sec[num_threads - 1];
                    const double delta = (ratio - 1.0) * 100.0;
                    printf("  %2zu threads: %f (%+.2f%% vs baseline)\n", num_threads, ops_per_sec, delta);
                }
                else
                {
                    printf("  %2zu threads: %f\n", num_threads, ops_per_sec);
                }
            }
        }
    }

    TEST(BitsetPerformanceTests, AcquireReleaseThreadScaling)
    {
        constexpr size_t MAX_THREADS = 16;
        const size_t max_threads = perf_config_get_thread_count("BITSET_PERF_MAX_THREADS", MAX_THREADS, MAX_THREADS);
        double lockfree_scale1_ops_per_sec[MAX_THREADS] = {};
        double lockfree_scale2_ops_per_sec[MAX_THREADS] = {};
        double lockfree_scale4_ops_per_sec[MAX_THREADS] = {};
        double mutex_global_ops_per_sec[MAX_THREADS] = {};
        double mutex_striped_ops_per_sec[MAX_THREADS] = {};

        size_t iterations_per_thread = perf_config_get_iterations("BITSET_PERF_ITERATIONS_MULTIPLIER", 5000000, 1, 100);
        iterations_per_thread = perf_config_get_uint("BITSET_PERF_ITERATIONS", iterations_per_thread, 1000, 500000000);

        const uint64_t base_seed = static_cast<uint64_t>(perf_config_get_uint("BITSET_PERF_SEED", 0x9e3779b97f4a7c15ULL, 1, SIZE_MAX));

        perf_report report("BitsetPerformanceTests", "AcquireReleaseThreadScaling");

        minstd::bitset<SIZE_IN_BITS, 1> lockfree_scale1;
        minstd::bitset<SIZE_IN_BITS, 2> lockfree_scale2;
        minstd::bitset<SIZE_IN_BITS, 4> lockfree_scale4;
        mutex_bitset_global mutex_global;
        mutex_bitset_striped mutex_striped;

        run_scaling_test<1>("lockfree scale=1", "Lockfree bitset scale=1", lockfree_scale1_ops_per_sec, nullptr, report,
                            perf_worker_lockfree<1>, &lockfree_scale1, nullptr, nullptr,
                            base_seed ^ 0x1001ULL, max_threads, iterations_per_thread);
        run_scaling_test<2>("lockfree scale=2", "Lockfree bitset scale=2", lockfree_scale2_ops_per_sec, lockfree_scale1_ops_per_sec, report,
                            perf_worker_lockfree<2>, &lockfree_scale2, nullptr, nullptr,
                            base_seed ^ 0x2002ULL, max_threads, iterations_per_thread);
        run_scaling_test<4>("lockfree scale=4", "Lockfree bitset scale=4", lockfree_scale4_ops_per_sec, lockfree_scale1_ops_per_sec, report,
                            perf_worker_lockfree<4>, &lockfree_scale4, nullptr, nullptr,
                            base_seed ^ 0x4004ULL, max_threads, iterations_per_thread);

        run_scaling_test<1>("mutex_global", "Mutex global bitset", mutex_global_ops_per_sec, lockfree_scale1_ops_per_sec, report,
                            perf_worker_mutex_global<1>, nullptr, &mutex_global, nullptr,
                            base_seed ^ 0x3003ULL, max_threads, iterations_per_thread);
        run_scaling_test<1>("mutex_striped", "Mutex striped bitset", mutex_striped_ops_per_sec, lockfree_scale1_ops_per_sec, report,
                            perf_worker_mutex_striped<1>, nullptr, nullptr, &mutex_striped,
                            base_seed ^ 0x5005ULL, max_threads, iterations_per_thread);

        report.finalize();
    }
}
