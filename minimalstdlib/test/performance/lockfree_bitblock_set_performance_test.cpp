// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/bitblock_set>

#include "../shared/perf_test_config.h"
#include "../shared/perf_report.h"

#include <atomic>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (BitblockSetPerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    namespace
    {
        minstd::atomic<bool> perf_start{false};

        constexpr size_t SIZE_IN_BITS = 16384;
        constexpr size_t WORD_BITS = 64;
        constexpr size_t WORD_COUNT = SIZE_IN_BITS / WORD_BITS;
        constexpr size_t STRIPES = 32;

        struct mutex_bitblock_set_global
        {
            uint64_t words_[WORD_COUNT] = {};
            pthread_mutex_t mutex_{};

            mutex_bitblock_set_global()
            {
                pthread_mutex_init(&mutex_, nullptr);
            }

            ~mutex_bitblock_set_global()
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

            bool acquire_first_available(size_t *index_out)
            {
                pthread_mutex_lock(&mutex_);
                for (size_t word = 0; word < WORD_COUNT; ++word)
                {
                    if (words_[word] == UINT64_MAX)
                    {
                        continue;
                    }

                    for (size_t bit = 0; bit < WORD_BITS; ++bit)
                    {
                        const uint64_t mask = 1ULL << bit;
                        if ((words_[word] & mask) == 0)
                        {
                            words_[word] |= mask;
                            *index_out = word * WORD_BITS + bit;
                            pthread_mutex_unlock(&mutex_);
                            return true;
                        }
                    }
                }

                pthread_mutex_unlock(&mutex_);
                return false;
            }

            void release(size_t index)
            {
                const size_t word = index / WORD_BITS;
                const uint64_t mask = 1ULL << (index % WORD_BITS);

                pthread_mutex_lock(&mutex_);
                words_[word] &= ~mask;
                pthread_mutex_unlock(&mutex_);
            }
        };

        struct mutex_bitblock_set_striped
        {
            uint64_t words_[WORD_COUNT] = {};
            pthread_mutex_t stripe_mutex_[STRIPES]{};

            mutex_bitblock_set_striped()
            {
                for (size_t i = 0; i < STRIPES; ++i)
                {
                    pthread_mutex_init(&stripe_mutex_[i], nullptr);
                }
            }

            ~mutex_bitblock_set_striped()
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

            bool acquire_first_available(size_t *index_out, uint64_t &rng_state)
            {
                const size_t start_stripe = static_cast<size_t>(rng_state % STRIPES);

                for (size_t offset = 0; offset < STRIPES; ++offset)
                {
                    const size_t stripe = (start_stripe + offset) % STRIPES;
                    pthread_mutex_lock(&stripe_mutex_[stripe]);

                    for (size_t word = stripe; word < WORD_COUNT; word += STRIPES)
                    {
                        if (words_[word] == UINT64_MAX)
                        {
                            continue;
                        }

                        for (size_t bit = 0; bit < WORD_BITS; ++bit)
                        {
                            const uint64_t mask = 1ULL << bit;
                            if ((words_[word] & mask) == 0)
                            {
                                words_[word] |= mask;
                                *index_out = word * WORD_BITS + bit;
                                pthread_mutex_unlock(&stripe_mutex_[stripe]);
                                return true;
                            }
                        }
                    }

                    pthread_mutex_unlock(&stripe_mutex_[stripe]);
                }

                return false;
            }

            void release(size_t index)
            {
                const size_t word = index / WORD_BITS;
                const size_t stripe = word % STRIPES;
                const uint64_t mask = 1ULL << (index % WORD_BITS);

                pthread_mutex_lock(&stripe_mutex_[stripe]);
                words_[word] &= ~mask;
                pthread_mutex_unlock(&stripe_mutex_[stripe]);
            }
        };

        struct alignas(64) perf_thread_args
        {
            minstd::bitblock_set<16384> *blocks_ = nullptr;
            mutex_bitblock_set_global *global_blocks_ = nullptr;
            mutex_bitblock_set_striped *striped_blocks_ = nullptr;
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

        void *perf_worker_lockfree(void *arg)
        {
            auto *args = static_cast<perf_thread_args *>(arg);

            while (!perf_start.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                auto result = args->blocks_->acquire_first_available();

                if (result.has_value())
                {
                    args->blocks_->release(result.value());
                }
            }

            return nullptr;
        }

        void *perf_worker_mutex_global(void *arg)
        {
            auto *args = static_cast<perf_thread_args *>(arg);

            while (!perf_start.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                size_t index = 0;
                if (args->global_blocks_->acquire_first_available(&index))
                {
                    args->global_blocks_->release(index);
                }
            }

            return nullptr;
        }

        void *perf_worker_mutex_striped(void *arg)
        {
            auto *args = static_cast<perf_thread_args *>(arg);

            while (!perf_start.load(minstd::memory_order_acquire))
            {
            }

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                size_t index = 0;
                xorshift64(args->rng_state_);
                if (args->striped_blocks_->acquire_first_available(&index, args->rng_state_))
                {
                    args->striped_blocks_->release(index);
                }
            }

            return nullptr;
        }

        double run_once(size_t num_threads,
                        size_t iterations_per_thread,
                        perf_thread_args *arguments,
                        pthread_t *threads,
                        void *(*worker)(void *),
                        uint64_t base_seed)
        {
            perf_start.store(false, minstd::memory_order_release);

            for (size_t i = 0; i < num_threads; ++i)
            {
                arguments[i].rng_state_ = base_seed ^ (i * 0x100000001b3ULL);
                arguments[i].iterations_ = iterations_per_thread;
                pthread_create(&threads[i], nullptr, worker, &arguments[i]);
            }

            timespec start_time{};
            timespec end_time{};
            clock_gettime(CLOCK_MONOTONIC, &start_time);
            perf_start.store(true, minstd::memory_order_release);

            for (size_t i = 0; i < num_threads; ++i)
            {
                pthread_join(threads[i], nullptr);
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);
            double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec);
            duration += static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9;
            return static_cast<double>(iterations_per_thread * num_threads) / duration;
        }

        void run_scaling_test(const char *series_label,
                              const char *display_label,
                              double *ops_per_sec_out,
                              const double *baseline_ops_per_sec,
                              perf_report &report,
                              perf_thread_args *arguments,
                              pthread_t *threads,
                              void *(*worker)(void *),
                              minstd::bitblock_set<SIZE_IN_BITS> *lockfree_blocks,
                              mutex_bitblock_set_global *global_blocks,
                              mutex_bitblock_set_striped *striped_blocks,
                              size_t iterations_per_thread,
                              uint64_t base_seed,
                              size_t max_threads)
        {
            const size_t warmup_iterations_per_thread = (iterations_per_thread / 100 > 1000) ? (iterations_per_thread / 100) : 1000;

            printf("%s acquire_first_available/release ops/sec:\n", display_label);

            for (size_t num_threads = 1; num_threads <= max_threads; ++num_threads)
            {
                if (global_blocks != nullptr)
                {
                    global_blocks->reset();
                }
                if (striped_blocks != nullptr)
                {
                    striped_blocks->reset();
                }

                run_once(num_threads, warmup_iterations_per_thread, arguments, threads, worker, base_seed ^ 0xa5a5a5a5ULL);
                const double ops_per_sec = run_once(num_threads, iterations_per_thread, arguments, threads, worker, base_seed);

                ops_per_sec_out[num_threads - 1] = ops_per_sec;

                char row_label[64];
                snprintf(row_label, sizeof(row_label), "%s %zu threads", series_label, num_threads);
                report.record(row_label, ops_per_sec, num_threads, iterations_per_thread);

                if (baseline_ops_per_sec != nullptr)
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

    TEST(BitblockSetPerformanceTests, AcquireFirstAvailableThreadScaling)
    {
        constexpr size_t MAX_THREADS = 16;
        const size_t max_threads = perf_config_get_thread_count("BITBLOCK_SET_PERF_MAX_THREADS", MAX_THREADS, MAX_THREADS);
        size_t iterations_per_thread = perf_config_get_iterations("BITBLOCK_SET_PERF_ITERATIONS_MULTIPLIER", 2000000, 1, 100);
        iterations_per_thread = perf_config_get_uint("BITBLOCK_SET_PERF_ITERATIONS", iterations_per_thread, 1000, 500000000);
        const uint64_t base_seed = static_cast<uint64_t>(perf_config_get_uint("BITBLOCK_SET_PERF_SEED", 0x9e3779b97f4a7c15ULL, 1, SIZE_MAX));

        minstd::bitblock_set<SIZE_IN_BITS> lockfree_blocks;
        mutex_bitblock_set_global global_blocks;
        mutex_bitblock_set_striped striped_blocks;

        pthread_t threads[MAX_THREADS];
        perf_thread_args arguments[MAX_THREADS];
        for (size_t i = 0; i < MAX_THREADS; ++i)
        {
            arguments[i].blocks_ = &lockfree_blocks;
            arguments[i].global_blocks_ = &global_blocks;
            arguments[i].striped_blocks_ = &striped_blocks;
        }

        double lockfree_ops_per_sec[MAX_THREADS] = {};
        double mutex_global_ops_per_sec[MAX_THREADS] = {};
        double mutex_striped_ops_per_sec[MAX_THREADS] = {};

        perf_report report("BitblockSetPerformanceTests", "AcquireFirstAvailableThreadScaling");

        run_scaling_test("lockfree", "Lockfree bitblock_set", lockfree_ops_per_sec, nullptr, report,
                         arguments, threads, perf_worker_lockfree,
                         &lockfree_blocks, nullptr, nullptr,
                         iterations_per_thread, base_seed ^ 0x1001ULL, max_threads);

        run_scaling_test("mutex_global", "Mutex global bitblock_set", mutex_global_ops_per_sec, lockfree_ops_per_sec, report,
                         arguments, threads, perf_worker_mutex_global,
                         nullptr, &global_blocks, nullptr,
                         iterations_per_thread, base_seed ^ 0x2002ULL, max_threads);

        run_scaling_test("mutex_striped", "Mutex striped bitblock_set", mutex_striped_ops_per_sec, lockfree_ops_per_sec, report,
                         arguments, threads, perf_worker_mutex_striped,
                         nullptr, nullptr, &striped_blocks,
                         iterations_per_thread, base_seed ^ 0x3003ULL, max_threads);

        report.finalize();
    }
}
