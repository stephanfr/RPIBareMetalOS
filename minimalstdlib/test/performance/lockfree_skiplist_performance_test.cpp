// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../shared/lockfree_skiplist_test_helpers.h"

namespace
{
    template <typename map_type>
    void run_mixed_write_load_rwlock_baseline(const char *label)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        const size_t iterations_per_thread = skiplist_mixed_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        struct write_load_config
        {
            const char *label;
            size_t write_period;
        };

        static const write_load_config configs[] = {
            {"0.1%", 1000},
            {"1%", 100},
            {"5%", 20},
            {"10%", 10},
            {"20%", 5},
        };

        printf("%s write-load perf: threads=%zu iterations/thread=%zu initial_occupancy=%zu%%\n",
             label, num_threads, iterations_per_thread, SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT);

        for (size_t cfg_idx = 0; cfg_idx < sizeof(configs) / sizeof(configs[0]); ++cfg_idx)
        {
            const write_load_config &cfg = configs[cfg_idx];

            map_type map;

            const uint32_t initial_count = static_cast<uint32_t>(SKIPLIST_STRESS_KEY_SPACE * SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT / 100u);
            for (uint32_t key = 0; key < initial_count; ++key)
            {
                CHECK_TRUE(map.insert(key, key));
            }

            minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            rwlock_map_write_load_thread_args<map_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

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
                thread_args[i].map_ = &map;
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

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, rwlock_map_write_load_worker<map_type>, &thread_args[i]));
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

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
                total_composite_allocations += thread_args[i].composite_allocations_;
                total_insert_successes += thread_args[i].insert_successes_;
                total_remove_successes += thread_args[i].remove_successes_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  write=%-5s ops/sec=%f inserts=%zu removes=%zu allocs=%zu\n",
                   cfg.label,
                   ops_per_sec,
                   total_insert_successes,
                   total_remove_successes,
                   total_composite_allocations);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(expected_operations, total_composite_allocations);
            CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistPerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    TEST(SkiplistPerformanceTests, PerfOnlyFindThreadScaling)
    {
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;
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
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

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

        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        minstd::pmr::lockfree_composite_single_arena_resource<1000, 64, 1024, 32, 512, false, 32 * 1024 * 1024, 5> composite_resource(
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

        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS, DEFAULT_MAX_LEVELS, 14, minstd::skiplist_extensions::skiplist_statistics>;

        const size_t iterations_per_thread = skiplist_mixed_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        struct write_load_config
        {
            const char *label;
            size_t write_period;
        };

        static const write_load_config configs[] = {
            {"0.1%", 1000},
            {"1%", 100},
            {"5%", 20},
            {"10%", 10},
            {"20%", 5},
        };

        printf("Skiplist + malloc/free wrapper write-load perf: threads=%zu iterations/thread=%zu initial_occupancy=%zu%%\n",
               num_threads, iterations_per_thread, SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT);

        uint32_t overall_slot_high_water = 0;

        for (size_t cfg_idx = 0; cfg_idx < sizeof(configs) / sizeof(configs[0]); ++cfg_idx)
        {
            const write_load_config &cfg = configs[cfg_idx];

            list_type::reset_slot_high_water_mark();

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

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            uint32_t slot_high_water = list_type::slot_high_water_mark();
            if (slot_high_water > overall_slot_high_water)
            {
                overall_slot_high_water = slot_high_water;
            }

            printf("  write=%-5s ops/sec=%f inserts=%zu removes=%zu allocs=%zu slot_high_water=%u\n",
                   cfg.label,
                   ops_per_sec,
                   total_insert_successes,
                   total_remove_successes,
                   total_composite_allocations,
                   slot_high_water);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(expected_operations, total_composite_allocations);
            CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
            CHECK_TRUE((total_insert_successes + total_remove_successes) > 0);
            CHECK_TRUE(checksum > 0);
        }

        printf("Skiplist mixed write-load overall slot high-water mark: %u\n", overall_slot_high_water);
        CHECK_TRUE(overall_slot_high_water > 0u);
    }

    TEST(SkiplistPerformanceTests, PerfMixedWriteLoadRWLockMapBaseline)
    {
        run_mixed_write_load_rwlock_baseline<rwlock_hash_map>("Reader/writer lock ordered map (minstd::avl_tree) baseline");
    }
}
