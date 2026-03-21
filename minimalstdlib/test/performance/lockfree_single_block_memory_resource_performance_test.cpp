// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../shared/lockfree_single_block_resource_test_helpers.h"
#include "../shared/perf_test_config.h"
#include "../shared/perf_report.h"

// clang-format off

TEST_GROUP(LockfreeSingleBlockMemoryResourcePerformanceTests)
{
};

using lockfree_single_block_resource_perf =
    minstd::pmr::lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy<
        minstd::pmr::platform::noop_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        32 * 1024 * 1024,
        5,
        minstd::pmr::extensions::null_memory_resource_statistics>;

TEST(LockfreeSingleBlockMemoryResourcePerformanceTests, MultiThreadAllocateDeallocateTest)
{
    constexpr size_t NUM_THREADS = 12;

    start_allocations = false;

    lockfree_single_block_resource_perf resource(buffer, BUFFER_SIZE);

    allocator_thread_arguments args[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &resource;
        args[i].rng_seed = i + 444;
        args[i].repetitions = 2000;

        CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
    }

    sleep(1);

    timespec start_time{};
    timespec end_time{};
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    start_allocations = true;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double duration = (end_time.tv_sec - start_time.tv_sec) +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    printf("Lockfree Single Block Resource Multithread Tests Duration: %f\n", duration);

    //  Do it again with malloc/free

    start_allocations = false;

    minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &malloc_free_resource;

        CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
    }

    sleep(1);

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    start_allocations = true;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    duration = (end_time.tv_sec - start_time.tv_sec) +
               (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    printf("Malloc Free Resource Multithread Tests Duration: %f\n", duration);
}

TEST(LockfreeSingleBlockMemoryResourcePerformanceTests, ThreadScalabilitySensitivityAnalysis)
{
    printf("\n=== Thread Scalability Analysis: Lockfree vs Malloc/Free Comparison ===\n");
    printf("Threads | Lockfree Ops/sec | Malloc Ops/sec | LF Efficiency | Malloc Eff | LF Scale | Speedup\n");
    printf("--------|------------------|----------------|---------------|------------|----------|----------\n");

    static double baseline_lockfree_efficiency = 0.0;
    const size_t MAX_THREADS = perf_config_get_thread_count("SINGLE_BLOCK_PERF_MAX_THREADS", 16, 64);

    perf_report report("LockfreeSingleBlockMemoryResourcePerformanceTests", "ThreadScalabilitySensitivityAnalysis");

    for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
    {
        size_t total_allocation_operations = num_threads * PERF_ALLOCATIONS_PER_THREAD;
        size_t total_deallocation_operations = num_threads * PERF_ALLOCATIONS_PER_THREAD * PERF_REPETITIONS;
        size_t total_operations = total_allocation_operations + total_deallocation_operations;

        // ===== Test Lockfree Allocator =====
        start_allocations = false;
        exit_thread = false;

        lockfree_single_block_resource_perf resource(buffer, BUFFER_SIZE);

        perf_thread_arguments args[64];
        pthread_t threads[64];

        for (size_t i = 0; i < num_threads; i++)
        {
            args[i].mem_resource = &resource;
            args[i].rng_seed = i + 1000 + num_threads * 100;
            args[i].repetitions = PERF_REPETITIONS;

            args[i].pointers_allocated.fill(nullptr);
            args[i].sizes_allocated.fill(0);

            CHECK(pthread_create(&threads[i], NULL, perf_repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
        }

        usleep(100000); // 100ms

        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start_allocations = true;

        for (size_t i = 0; i < num_threads; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        double lockfree_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                       (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

        double lockfree_total_ops_per_second = total_operations / lockfree_elapsed_time;
        double lockfree_efficiency = lockfree_total_ops_per_second / num_threads;

        // ===== Test Malloc/Free =====
        start_allocations = false;

        minstd::pmr::malloc_free_wrapper_memory_resource malloc_resource(nullptr);

        for (size_t i = 0; i < num_threads; i++)
        {
            args[i].mem_resource = &malloc_resource;
            args[i].rng_seed = i + 2000 + num_threads * 100;

            args[i].pointers_allocated.fill(nullptr);
            args[i].sizes_allocated.fill(0);

            CHECK(pthread_create(&threads[i], NULL, perf_repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
        }

        usleep(100000); // 100ms

        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start_allocations = true;

        for (size_t i = 0; i < num_threads; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        double malloc_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

        double malloc_total_ops_per_second = total_operations / malloc_elapsed_time;
        double malloc_efficiency = malloc_total_ops_per_second / num_threads;

        if (num_threads == 1)
        {
            baseline_lockfree_efficiency = lockfree_efficiency;
        }

        double lockfree_scalability = baseline_lockfree_efficiency > 0 ? (lockfree_efficiency / baseline_lockfree_efficiency) : 1.0;
        double speedup = malloc_total_ops_per_second > 0 ? (lockfree_total_ops_per_second / malloc_total_ops_per_second) : 0.0;

        printf("  %2zu   | %14.0f | %12.0f | %11.0f | %8.0f | %6.2fx | %6.2fx\n",
               num_threads, lockfree_total_ops_per_second, malloc_total_ops_per_second,
               lockfree_efficiency, malloc_efficiency, lockfree_scalability, speedup);

        char label[64];
        snprintf(label, sizeof(label), "lockfree %zu threads", num_threads);

        const double delta_percent = (malloc_total_ops_per_second > 0.0)
                         ? ((lockfree_total_ops_per_second - malloc_total_ops_per_second) * 100.0 / malloc_total_ops_per_second)
                         : 0.0;

        report.record_with_baseline(label,
                        lockfree_total_ops_per_second,
                        malloc_total_ops_per_second,
                        delta_percent,
                        speedup,
                        num_threads,
                        PERF_ALLOCATIONS_PER_THREAD * PERF_REPETITIONS);

        snprintf(label, sizeof(label), "malloc/free %zu threads", num_threads);
        report.record(label, malloc_total_ops_per_second, num_threads, PERF_ALLOCATIONS_PER_THREAD * PERF_REPETITIONS);

        CHECK(lockfree_elapsed_time > 0);
        CHECK(malloc_elapsed_time > 0);
        CHECK(lockfree_total_ops_per_second > 0);
        CHECK(malloc_total_ops_per_second > 0);

        usleep(100000); // 100ms
    }

    printf("\nNotes:\n");
    printf("- Ops/sec: Total allocation + deallocation operations per second\n");
    printf("- Efficiency: Total operations per second per thread\n");
    printf("- LF Scale: Lockfree efficiency relative to single-thread baseline\n");
    printf("- Speedup: Lockfree vs Malloc performance ratio (>1.0 = lockfree faster)\n");
    printf("- Each thread performs %zu initial allocations + %zu repetitions\n",
           PERF_ALLOCATIONS_PER_THREAD, PERF_REPETITIONS);
    printf("- Allocation sizes: lognormal(5.4, 1.2) distribution, clamped to [1, %zu] bytes\n",
           PERF_MAX_ALLOCATION_SIZE);
    printf("- Both allocators tested with identical workloads for fair comparison\n");

    report.finalize();
}
