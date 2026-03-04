// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <CppUTest/MemoryLeakWarningPlugin.h>

#include <minstdconfig.h>

#include <lockfree/skiplist>
#include <__memory_resource/composite_pool_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

namespace
{
    static constexpr size_t SKIPLIST_STRESS_MAX_THREADS = 16;
    static constexpr size_t SKIPLIST_STRESS_NUM_THREADS = 8;
    static constexpr size_t SKIPLIST_STRESS_ITERATIONS_PER_THREAD = 20000;
    static constexpr uint32_t SKIPLIST_STRESS_KEY_SPACE = 4096;
    static constexpr size_t SKIPLIST_PERF_DEFAULT_MULTIPLIER = 500;
    static constexpr size_t SKIPLIST_MIXED_DEFAULT_MULTIPLIER = 10;
    static constexpr size_t SKIPLIST_COMPOSITE_BUFFER_SIZE = 64 * 1024 * 1024;
    static constexpr size_t SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT = 50;

    char* skiplist_composite_resource_buffer = new char[SKIPLIST_COMPOSITE_BUFFER_SIZE]();

    size_t skiplist_scaling_iterations_per_thread()
    {
        const char *multiplier_text = getenv("SKIPLIST_STRESS_ITERATIONS_MULTIPLIER");

        const size_t default_iterations_per_thread = SKIPLIST_STRESS_ITERATIONS_PER_THREAD * SKIPLIST_PERF_DEFAULT_MULTIPLIER;

        if ((multiplier_text == nullptr) || (multiplier_text[0] == '\0'))
        {
            return default_iterations_per_thread;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_multiplier = strtoul(multiplier_text, &parse_end, 10);

        if ((parse_end == multiplier_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return default_iterations_per_thread;
        }

        if (parsed_multiplier == 0ul)
        {
            return default_iterations_per_thread;
        }

        constexpr unsigned long max_multiplier = 10000ul;
        const unsigned long clamped_multiplier = (parsed_multiplier > max_multiplier) ? max_multiplier : parsed_multiplier;

        return SKIPLIST_STRESS_ITERATIONS_PER_THREAD * static_cast<size_t>(clamped_multiplier);
    }

    size_t skiplist_mixed_iterations_per_thread()
    {
        const char *multiplier_text = getenv("SKIPLIST_MIXED_ITERATIONS_MULTIPLIER");

        const size_t default_iterations_per_thread = SKIPLIST_STRESS_ITERATIONS_PER_THREAD * SKIPLIST_MIXED_DEFAULT_MULTIPLIER;

        if ((multiplier_text == nullptr) || (multiplier_text[0] == '\0'))
        {
            return default_iterations_per_thread;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_multiplier = strtoul(multiplier_text, &parse_end, 10);

        if ((parse_end == multiplier_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return default_iterations_per_thread;
        }

        if (parsed_multiplier == 0ul)
        {
            return default_iterations_per_thread;
        }

        constexpr unsigned long max_multiplier = 100ul;
        const unsigned long clamped_multiplier = (parsed_multiplier > max_multiplier) ? max_multiplier : parsed_multiplier;

        return SKIPLIST_STRESS_ITERATIONS_PER_THREAD * static_cast<size_t>(clamped_multiplier);
    }

    size_t skiplist_perf_num_arenas()
    {
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

        if (cpu_count < 1)
        {
            return 1;
        }

        return static_cast<size_t>(cpu_count);
    }

    size_t skiplist_perf_thread_count()
    {
        const char *thread_count_text = getenv("SKIPLIST_PERF_THREADS");

        if ((thread_count_text == nullptr) || (thread_count_text[0] == '\0'))
        {
            return SKIPLIST_STRESS_NUM_THREADS;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_thread_count = strtoul(thread_count_text, &parse_end, 10);

        if ((parse_end == thread_count_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return SKIPLIST_STRESS_MAX_THREADS;
        }

        if (parsed_thread_count == 0ul)
        {
            return 1;
        }

        if (parsed_thread_count > SKIPLIST_STRESS_MAX_THREADS)
        {
            return SKIPLIST_STRESS_MAX_THREADS;
        }

        return static_cast<size_t>(parsed_thread_count);
    }

    template <typename list_type>
    struct skiplist_stress_thread_args
    {
        list_type *list_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *validation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
    };

    template <typename list_type>
    struct skiplist_write_correctness_thread_args
    {
        list_type *list_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *validation_failures_ = nullptr;
        bool *expected_present_ = nullptr;
        uint32_t thread_id_ = 0;
        uint32_t num_threads_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
    };

    template <typename list_type>
    struct skiplist_perf_thread_args
    {
        list_type *list_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        uint64_t checksum_ = 0;
    };

    template <typename list_type>
    struct skiplist_perf_composite_thread_args
    {
        list_type *list_ = nullptr;
        minstd::pmr::memory_resource *memory_resource_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *allocation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        size_t composite_allocations_ = 0;
        uint64_t checksum_ = 0;
    };

    template <typename list_type>
    struct skiplist_perf_write_load_thread_args
    {
        list_type *list_ = nullptr;
        minstd::pmr::memory_resource *memory_resource_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *allocation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t write_period_ = 0;  // 0 = reads only; N = 1-in-N ops is a write
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        size_t composite_allocations_ = 0;
        size_t insert_successes_ = 0;
        size_t remove_successes_ = 0;
        uint64_t checksum_ = 0;
        uint64_t rng_seed_ = 0;
    };

    struct memory_leak_overload_scope_guard
    {
        memory_leak_overload_scope_guard()
        {
            MemoryLeakWarningPlugin::saveAndDisableNewDeleteOverloads();
        }

        ~memory_leak_overload_scope_guard()
        {
            MemoryLeakWarningPlugin::restoreNewDeleteOverloads();
        }
    };


    template <typename list_type>
    void *skiplist_stress_worker(void *arg)
    {
        auto *args = static_cast<skiplist_stress_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((i + (args->thread_id_ * 97u)) % args->key_space_);
            auto *value = args->list_->find(key);

            if ((value == nullptr) || (*value != key))
            {
                args->validation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }

            args->operations_completed_++;
        }

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_write_correctness_worker(void *arg)
    {
        auto *args = static_cast<skiplist_write_correctness_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((args->thread_id_ + (i * args->num_threads_)) % args->key_space_);

            if ((i & 1u) == 0u)
            {
                if (args->list_->insert(key, key))
                {
                    args->expected_present_[key] = true;
                }
            }
            else
            {
                if (args->list_->remove(key))
                {
                    args->expected_present_[key] = false;
                }
            }

            if ((i % 8u) == 0u)
            {
                auto *value = args->list_->find(key);

                if ((value != nullptr) && (*value != key))
                {
                    args->validation_failures_->fetch_add(1, minstd::memory_order_relaxed);
                }
            }

            args->operations_completed_++;
        }

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_perf_worker(void *arg)
    {
        auto *args = static_cast<skiplist_perf_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((i + (args->thread_id_ * 97u)) % args->key_space_);
            auto *value = args->list_->find(key);

            if (value != nullptr)
            {
                checksum += *value;
            }

            args->operations_completed_++;
        }

        args->checksum_ = checksum;

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_perf_composite_worker(void *arg)
    {
        auto *args = static_cast<skiplist_perf_composite_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t composite_allocations = 0;

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((i + (args->thread_id_ * 97u)) % args->key_space_);
            auto *value = args->list_->find(key);

            if (value != nullptr)
            {
                checksum += *value;
            }

            const size_t block_size = 32 + static_cast<size_t>((key + static_cast<uint32_t>(i)) & 0xFFu);
            void *ptr = args->memory_resource_->allocate(block_size);

            if (ptr == nullptr)
            {
                args->allocation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }
            else
            {
                args->memory_resource_->deallocate(ptr, block_size);
                composite_allocations++;
            }

            args->operations_completed_++;
        }

        args->composite_allocations_ = composite_allocations;
        args->checksum_ = checksum;

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_perf_write_load_worker(void *arg)
    {
        auto *args = static_cast<skiplist_perf_write_load_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t composite_allocations = 0;
        size_t insert_successes = 0;
        size_t remove_successes = 0;

        uint64_t rng_state = args->rng_seed_;

        if (rng_state == 0)
        {
            rng_state = 0x9e3779b97f4a7c15ull ^
                        (static_cast<uint64_t>(args->thread_id_) << 32) ^
                        static_cast<uint64_t>(args->iterations_);
        }

        auto next_random = [&rng_state]() -> uint64_t
        {
            rng_state ^= (rng_state << 13);
            rng_state ^= (rng_state >> 7);
            rng_state ^= (rng_state << 17);
            return rng_state;
        };

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>(next_random() % static_cast<uint64_t>(args->key_space_));

            const size_t block_size = 32 + static_cast<size_t>(next_random() & 0xFFu);
            void *ptr = args->memory_resource_->allocate(block_size);

            if (ptr == nullptr)
            {
                args->allocation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }
            else
            {
                args->memory_resource_->deallocate(ptr, block_size);
                composite_allocations++;
            }

            const bool do_write = (args->write_period_ > 0) && ((next_random() % args->write_period_) == 0);

            if (!do_write)
            {
                auto *value = args->list_->find(key);

                if (value != nullptr)
                {
                    checksum += *value;
                }
            }
            else
            {
                if ((next_random() & 1ull) == 0ull)
                {
                    if (args->list_->remove(key))
                    {
                        remove_successes++;
                    }
                }
                else
                {
                    if (args->list_->insert(key, key))
                    {
                        insert_successes++;
                    }
                }
            }

            args->operations_completed_++;
        }

        args->composite_allocations_ = composite_allocations;
        args->insert_successes_ = insert_successes;
        args->remove_successes_ = remove_successes;
        args->checksum_ = checksum;

        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistTests)
    {
    };
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistWriteCorrectnessTests)
    {
    };
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistPerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    TEST(SkiplistTests, BasicFunctionality)
    {
        minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS> list;

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK_TRUE(list.insert(i, i));
        }

        CHECK_EQUAL(100u, list.size());

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK_TRUE(list.find(i) != nullptr);
            CHECK_EQUAL(i, *list.find(i));
        }

        CHECK_TRUE(list.find(101) == nullptr);

        for (uint32_t i = 0; i < 10; i++)
        {
            CHECK_TRUE(list.remove(i * 10));
            CHECK_TRUE(list.remove((i * 10) + 1));
        }

        CHECK_EQUAL(80u, list.size());

        for (uint32_t i = 0; i < 100; i++)
        {
            if ((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0)))
            {
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }

        CHECK_FALSE(list.insert(14, 114));
        CHECK_EQUAL(14u, *list.find(14));

        CHECK_FALSE(list.insert(37, 137));
        CHECK_EQUAL(37u, *list.find(37));

        CHECK_TRUE(list.insert(100, 100));

        CHECK_EQUAL(81u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }

        list.gc();
        list.gc();

        CHECK_EQUAL(81u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }

        CHECK_TRUE(list.insert(10, 10));
        CHECK_TRUE(list.insert(31, 31));
        CHECK_TRUE(list.insert(71, 71));

        CHECK_EQUAL(84u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) &&
                (i != 10) && (i != 31) && (i != 71) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }
    }

    TEST(SkiplistTests, TemplateInstantiationWithCustomMaxLevels)
    {
        minstd::SkipList<uint32_t, uint64_t, SKIPLIST_STRESS_MAX_THREADS, 8> list;

        for (uint32_t i = 0; i < 256; ++i)
        {
            CHECK_TRUE(list.insert(i, static_cast<uint64_t>(i) * 10ull));
        }

        CHECK_EQUAL(256u, list.size());

        for (uint32_t i = 0; i < 256; ++i)
        {
            auto *value = list.find(i);
            CHECK_TRUE(value != nullptr);
            CHECK_EQUAL(static_cast<uint64_t>(i) * 10ull, *value);
        }

        for (uint32_t i = 0; i < 128; ++i)
        {
            CHECK_TRUE(list.remove(i));
        }

        list.gc();
        list.gc();

        for (uint32_t i = 0; i < 128; ++i)
        {
            CHECK_TRUE(list.find(i) == nullptr);
        }

        for (uint32_t i = 128; i < 256; ++i)
        {
            auto *value = list.find(i);
            CHECK_TRUE(value != nullptr);
            CHECK_EQUAL(static_cast<uint64_t>(i) * 10ull, *value);
        }
    }

    TEST(SkiplistTests, MultiThreadedStressInsertFindRemove)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        list_type list;

        for (uint32_t i = 0; i < SKIPLIST_STRESS_KEY_SPACE; ++i)
        {
            CHECK_TRUE(list.insert(i, i));
        }

        pthread_t workers[SKIPLIST_STRESS_NUM_THREADS]{};
        skiplist_stress_thread_args<list_type> thread_args[SKIPLIST_STRESS_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> validation_failures{0};

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].validation_failures_ = &validation_failures;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = SKIPLIST_STRESS_ITERATIONS_PER_THREAD;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_stress_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < SKIPLIST_STRESS_NUM_THREADS)
        {
            sched_yield();
        }

        timespec start_time{};
        timespec end_time{};
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        const size_t expected_operations = SKIPLIST_STRESS_NUM_THREADS * SKIPLIST_STRESS_ITERATIONS_PER_THREAD;
        const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        const double ops_per_sec = static_cast<double>(total_operations) / duration;

        printf("Skiplist stress (fixed %zu threads): %f ops/sec\n", SKIPLIST_STRESS_NUM_THREADS, ops_per_sec);

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));
        CHECK_TRUE(duration > 0.0);
        CHECK_TRUE(ops_per_sec > 0.0);

        CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());
    }

    TEST(SkiplistTests, MultiThreadedStressThreadScaling)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;
        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();

        printf("Skiplist concurrent find ops/sec (iterations/thread=%zu):\n", iterations_per_thread);

        for (size_t num_threads = 1; num_threads <= SKIPLIST_STRESS_MAX_THREADS; ++num_threads)
        {
            list_type list;

            for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
            {
                CHECK_TRUE(list.insert(key, key));
            }

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            skiplist_stress_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};
            minstd::atomic<size_t> validation_failures{0};

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].list_ = &list;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].validation_failures_ = &validation_failures;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_stress_worker<list_type>, &thread_args[i]));
            }

            while (ready_count.load(minstd::memory_order_acquire) < num_threads)
            {
                sched_yield();
            }

            timespec start_time{};
            timespec end_time{};
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            start.store(true, minstd::memory_order_release);

            size_t total_operations = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  %2zu threads: %f\n", num_threads, ops_per_sec);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);

            CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());

#ifdef __MINIMAL_STD_TEST__
            CHECK_TRUE(list.debug_validate_ordering());
#endif
        }
    }

    TEST(SkiplistPerformanceTests, PerfOnlyFindThreadScaling)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;
        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();

        printf("Skiplist perf-only find ops/sec (iterations/thread=%zu):\n", iterations_per_thread);

        for (size_t num_threads = 1; num_threads <= SKIPLIST_STRESS_MAX_THREADS; ++num_threads)
        {
            list_type list;

            for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
            {
                CHECK_TRUE(list.insert(key, key));
            }

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            skiplist_perf_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].list_ = &list;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;
                thread_args[i].checksum_ = 0;

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_worker<list_type>, &thread_args[i]));
            }

            while (ready_count.load(minstd::memory_order_acquire) < num_threads)
            {
                sched_yield();
            }

            timespec start_time{};
            timespec end_time{};
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            start.store(true, minstd::memory_order_release);

            size_t total_operations = 0;
            uint64_t checksum = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
                checksum += thread_args[i].checksum_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  %2zu threads: %f\n", num_threads, ops_per_sec);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
            CHECK_TRUE(checksum > 0);
        }
    }

    TEST(SkiplistPerformanceTests, PerfOnlyFindFixedThreadCount)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
        skiplist_perf_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};

        for (size_t i = 0; i < num_threads; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = iterations_per_thread;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;
            thread_args[i].checksum_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < num_threads)
        {
            sched_yield();
        }

        timespec start_time{};
        timespec end_time{};
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;
        uint64_t checksum = 0;

        for (size_t i = 0; i < num_threads; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
            checksum += thread_args[i].checksum_;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        const size_t expected_operations = num_threads * iterations_per_thread;
        const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        const double ops_per_sec = static_cast<double>(total_operations) / duration;

        printf("Skiplist perf-only fixed threads: threads=%zu iterations/thread=%zu ops/sec=%f\n",
               num_threads,
               iterations_per_thread,
               ops_per_sec);

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_TRUE(duration > 0.0);
        CHECK_TRUE(ops_per_sec > 0.0);
        CHECK_TRUE(checksum > 0);
    }

    TEST(SkiplistPerformanceTests, PerfOnlyFindFixedThreadCountWithCompositeResource)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        minstd::pmr::composite_pool_resource<1000, 64, 1024, 32, 512, false> composite_resource(
            skiplist_composite_resource_buffer,
            SKIPLIST_COMPOSITE_BUFFER_SIZE,
            skiplist_perf_num_arenas());

        pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
        skiplist_perf_composite_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> allocation_failures{0};

        for (size_t i = 0; i < num_threads; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].memory_resource_ = &composite_resource;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].allocation_failures_ = &allocation_failures;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = iterations_per_thread;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;
            thread_args[i].composite_allocations_ = 0;
            thread_args[i].checksum_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_composite_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < num_threads)
        {
            sched_yield();
        }

        timespec start_time{};
        timespec end_time{};
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;
        size_t total_composite_allocations = 0;
        uint64_t checksum = 0;

        for (size_t i = 0; i < num_threads; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
            total_composite_allocations += thread_args[i].composite_allocations_;
            checksum += thread_args[i].checksum_;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        const size_t expected_operations = num_threads * iterations_per_thread;
        const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        const double ops_per_sec = static_cast<double>(total_operations) / duration;

        printf("Skiplist + composite perf fixed threads: threads=%zu iterations/thread=%zu find_ops/sec=%f composite_allocations=%zu\n",
               num_threads,
               iterations_per_thread,
               ops_per_sec,
               total_composite_allocations);

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_EQUAL(expected_operations, total_composite_allocations);
        CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
        CHECK_TRUE(duration > 0.0);
        CHECK_TRUE(ops_per_sec > 0.0);
        CHECK_TRUE(checksum > 0);
    }

    TEST(SkiplistPerformanceTests, PerfMixedWriteLoadWithCompositeResource)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        const size_t iterations_per_thread = skiplist_mixed_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        // Write load configurations: label + write_period (1-in-N ops is a write; 0 = reads only)
        struct write_load_config
        {
            const char *label;
            size_t write_period;  // 0 = read-only; N = 1-in-N ops is a write
        };

        static const write_load_config configs[] = {
            {"0.1%",  1000},
            {"1%",    100},
            {"5%",    20},
            {"10%",   10},
            {"20%",   5},
        };

        printf("Skiplist + malloc/free wrapper write-load perf: threads=%zu iterations/thread=%zu initial_occupancy=%zu%%\n",
               num_threads, iterations_per_thread, SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT);

        for (size_t cfg_idx = 0; cfg_idx < sizeof(configs) / sizeof(configs[0]); ++cfg_idx)
        {
            const write_load_config &cfg = configs[cfg_idx];

            list_type list;

            const uint32_t initial_count = static_cast<uint32_t>(SKIPLIST_STRESS_KEY_SPACE * SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT / 100u);
            for (uint32_t key = 0; key < initial_count; ++key)
            {
                CHECK_TRUE(list.insert(key, key));
            }

            minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            skiplist_perf_write_load_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};
            minstd::atomic<size_t> allocation_failures{0};

            timespec seed_time{};
            clock_gettime(CLOCK_MONOTONIC, &seed_time);
            const uint64_t run_seed =
                (static_cast<uint64_t>(seed_time.tv_sec) << 32) ^
                static_cast<uint64_t>(seed_time.tv_nsec) ^
                (static_cast<uint64_t>(cfg_idx) * 0x9e3779b97f4a7c15ull) ^
                static_cast<uint64_t>(cfg.write_period);

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].list_ = &list;
                thread_args[i].memory_resource_ = &malloc_free_resource;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].allocation_failures_ = &allocation_failures;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].write_period_ = cfg.write_period;
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;
                thread_args[i].composite_allocations_ = 0;
                thread_args[i].insert_successes_ = 0;
                thread_args[i].remove_successes_ = 0;
                thread_args[i].checksum_ = 0;
                thread_args[i].rng_seed_ = run_seed ^
                                           (static_cast<uint64_t>(i + 1) * 0xbf58476d1ce4e5b9ull) ^
                                           (static_cast<uint64_t>(thread_args[i].thread_id_) << 17);

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_write_load_worker<list_type>, &thread_args[i]));
            }

            while (ready_count.load(minstd::memory_order_acquire) < num_threads)
            {
                sched_yield();
            }

            timespec start_time{};
            timespec end_time{};
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            start.store(true, minstd::memory_order_release);

            size_t total_operations = 0;
            size_t total_composite_allocations = 0;
            size_t total_insert_successes = 0;
            size_t total_remove_successes = 0;
            uint64_t checksum = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
                total_composite_allocations += thread_args[i].composite_allocations_;
                total_insert_successes += thread_args[i].insert_successes_;
                total_remove_successes += thread_args[i].remove_successes_;
                checksum += thread_args[i].checksum_;
            }

            const size_t total_gc_reclaimed_nodes = list.gc();

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  write=%-5s ops/sec=%f inserts=%zu removes=%zu gc_nodes=%zu allocs=%zu\n",
                   cfg.label,
                   ops_per_sec,
                   total_insert_successes,
                   total_remove_successes,
                   total_gc_reclaimed_nodes,
                   total_composite_allocations);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(expected_operations, total_composite_allocations);
            CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
            CHECK_TRUE((total_insert_successes + total_remove_successes) > 0);
            CHECK_TRUE(checksum > 0);
        }
    }


    TEST(SkiplistTests, MultiThreadedStressOrderingAndContentCorrectness)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        pthread_t workers[SKIPLIST_STRESS_NUM_THREADS]{};
        skiplist_stress_thread_args<list_type> thread_args[SKIPLIST_STRESS_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> validation_failures{0};

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].validation_failures_ = &validation_failures;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = SKIPLIST_STRESS_ITERATIONS_PER_THREAD * 2;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_stress_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < SKIPLIST_STRESS_NUM_THREADS)
        {
            sched_yield();
        }

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        CHECK_EQUAL(SKIPLIST_STRESS_NUM_THREADS * (SKIPLIST_STRESS_ITERATIONS_PER_THREAD * 2), total_operations);
        CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            auto *value = list.find(key);
            CHECK_TRUE(value != nullptr);
            CHECK_EQUAL(key, *value);
        }

        CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());

#ifdef __MINIMAL_STD_TEST__
        CHECK_TRUE(list.debug_validate_ordering());
#endif
    }

    TEST(SkiplistWriteCorrectnessTests, ConcurrentInsertContentOrderingThenSequentialRemove)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::SkipList<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        static constexpr size_t WRITE_TEST_NUM_THREADS = 8;
        static constexpr size_t WRITE_TEST_ITERATIONS_PER_THREAD = 4096;
        static constexpr uint32_t WRITE_TEST_KEY_SPACE = 2048;

        list_type list;

        bool expected_present[WRITE_TEST_KEY_SPACE]{};

        pthread_t workers[WRITE_TEST_NUM_THREADS]{};
        skiplist_write_correctness_thread_args<list_type> thread_args[WRITE_TEST_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> validation_failures{0};

        for (size_t i = 0; i < WRITE_TEST_NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].validation_failures_ = &validation_failures;
            thread_args[i].expected_present_ = expected_present;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].num_threads_ = static_cast<uint32_t>(WRITE_TEST_NUM_THREADS);
            thread_args[i].iterations_ = WRITE_TEST_ITERATIONS_PER_THREAD;
            thread_args[i].key_space_ = WRITE_TEST_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_write_correctness_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < WRITE_TEST_NUM_THREADS)
        {
            sched_yield();
        }

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < WRITE_TEST_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        CHECK_EQUAL(WRITE_TEST_NUM_THREADS * WRITE_TEST_ITERATIONS_PER_THREAD, total_operations);
        CHECK_EQUAL(0u, validation_failures.load(minstd::memory_order_relaxed));

        list.gc();
        list.gc();

        uint32_t expected_size = 0;

        for (uint32_t key = 0; key < WRITE_TEST_KEY_SPACE; ++key)
        {
            auto *value = list.find(key);

            if (expected_present[key])
            {
                CHECK_TRUE(value != nullptr);
                CHECK_EQUAL(key, *value);
                expected_size++;
            }
            else
            {
                CHECK_TRUE(value == nullptr);
            }
        }

        CHECK_EQUAL(expected_size, list.size());

#ifdef __MINIMAL_STD_TEST__
        CHECK_TRUE(list.debug_validate_ordering());
#endif

        for (uint32_t key = 0; key < WRITE_TEST_KEY_SPACE; ++key)
        {
            if (expected_present[key])
            {
                CHECK_TRUE(list.remove(key));
            }
            else
            {
                CHECK_FALSE(list.remove(key));
            }

            CHECK_TRUE(list.find(key) == nullptr);
        }

        list.gc();
        list.gc();

        CHECK_EQUAL(0u, list.size());

    #ifdef __MINIMAL_STD_TEST__
        CHECK_TRUE(list.debug_validate_ordering());
    #endif
    }
}
