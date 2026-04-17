// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_arena_resource.h>
#include <__memory_resource/__extensions/lockfree_single_arena_resource_extended_statistics.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include "../shared/interrupt_simulation_test_helpers.h"

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

extern "C" double sqrt(double);
extern "C" double log(double);
extern "C" double cos(double);
extern "C" double exp(double);

namespace
{
    constexpr size_t DEFAULT_ALIGNMENT = alignof(max_align_t);

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char *buffer = new char[BUFFER_SIZE]();

    

    typedef minstd::pmr::lockfree_single_arena_resource_impl<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        128 * 1024 * 1024,
        5,
        128,
        minstd::pmr::extensions::lockfree_single_arena_resource_extended_statistics,
        minstd::pmr::extensions::memory_resource_statistics> lockfree_single_arena_resource_with_stats;
        
    typedef minstd::pmr::lockfree_single_arena_resource_impl<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        128 * 1024 * 1024,
        5,
        128,
        minstd::pmr::extensions::lockfree_single_arena_resource_extended_statistics,
        minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_arena_resource_without_stats;
}

#include "../shared/perf_test_config.h"
#include "../shared/perf_report.h"

// clang-format off

namespace
{
    constexpr size_t PERF_ALLOCATIONS_PER_THREAD = 2000;
    constexpr size_t PERF_MAX_ALLOCATION_SIZE = 16384;
    constexpr size_t PERF_REPETITIONS = 500;

    size_t lognormal_sample(minstd::Xoroshiro128PlusPlusRNG &rng)
    {
        double u1, u2;
        do
        {
            u1 = static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
            u2 = static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
        } while (u1 == 0.0);

        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
        double value = exp(5.4 + 1.2 * z);
        return (value < 1.0) ? 1 : static_cast<size_t>(value);
    }
    minstd::atomic<bool> start_allocations = false;
    minstd::atomic<bool> exit_thread = false;
    minstd::atomic<bool> correctness_allocation_failed = false;
    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 5000;

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        bool reduced_pressure_for_correctness = false;
        minstd::array<void *, NUM_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deleted_element = {false};
        size_t repetitions = 0;
        size_t allocation_failures = 0;
    };

    struct perf_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, PERF_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, PERF_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        size_t repetitions = 0;
        size_t allocation_failures = 0;
    };
    void *repeated_allocation_deallocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        while (!start_allocations)
        {
            // spin wait
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t block_size = 128 + (rng() % 7000);

            void *ptr = nullptr;
            do
            {
                ptr = args->mem_resource->allocate(block_size);
                if (ptr == nullptr)
                {
                    args->allocation_failures++;
                }
            } while (ptr == nullptr);

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = block_size;
        }

        for (size_t j = 0; j < args->repetitions; j++)
        {
            for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);

                size_t block_size = 128 + (rng() % 7000);

                void *ptr = nullptr;
                do
                {
                    ptr = args->mem_resource->allocate(block_size);
                    if (ptr == nullptr)
                    {
                        args->allocation_failures++;
                    }
                } while (ptr == nullptr);

                args->pointers_allocated[i] = ptr;
                args->sizes_allocated[i] = block_size;
            }
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    void *perf_repeated_allocation_deallocation_thread(void *arguments)
    {
        perf_thread_arguments *args = static_cast<perf_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        while (!start_allocations)
        {
            // spin wait
        }

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t block_size = lognormal_sample(rng);
            if (block_size > PERF_MAX_ALLOCATION_SIZE)
                block_size = PERF_MAX_ALLOCATION_SIZE;

            void *ptr = nullptr;
            do
            {
                ptr = args->mem_resource->allocate(block_size);
                if (ptr == nullptr)
                {
                    args->allocation_failures++;
                }
            } while (ptr == nullptr);

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = block_size;
        }

        for (size_t j = 0; j < args->repetitions; j++)
        {
            for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);

                size_t block_size = lognormal_sample(rng);
                if (block_size > PERF_MAX_ALLOCATION_SIZE)
                    block_size = PERF_MAX_ALLOCATION_SIZE;

                void *ptr = nullptr;
                do
                {
                    ptr = args->mem_resource->allocate(block_size);
                    if (ptr == nullptr)
                    {
                        args->allocation_failures++;
                    }
                } while (ptr == nullptr);

                args->pointers_allocated[i] = ptr;
                args->sizes_allocated[i] = block_size;
            }
        }

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }


}



TEST_GROUP(LockfreeSingleArenaMemoryResourcePerformanceTests)
{
};

using lockfree_single_arena_resource_perf =
    minstd::pmr::lockfree_single_arena_resource_impl<
        minstd::pmr::platform::noop_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        64 * 1024 * 1024,
        5,
        128,
        minstd::pmr::extensions::null_memory_resource_statistics>;

TEST(LockfreeSingleArenaMemoryResourcePerformanceTests, MultiThreadAllocateDeallocateTest)
{
    constexpr size_t NUM_THREADS = 12;

    start_allocations = false;

    lockfree_single_arena_resource_perf resource(buffer, BUFFER_SIZE, minstd::pmr::test::os_abstractions::get_cpu_count());

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

    size_t total_failures = 0;
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
        total_failures += args[i].allocation_failures;
    }
    
    if (total_failures > 0)
    {
        printf("[%zu Threads] Lockfree correctness test allocation failures: %zu\n", NUM_THREADS, total_failures);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double duration = (end_time.tv_sec - start_time.tv_sec) +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    printf("Lockfree Single Arena Resource Multithread Tests Duration: %f\n", duration);

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

TEST(LockfreeSingleArenaMemoryResourcePerformanceTests, ThreadScalabilitySensitivityAnalysis)
{
    printf("\n=== Thread Scalability Analysis: Lockfree vs Malloc/Free Comparison ===\n");
    printf("Threads | Lockfree Ops/sec | Malloc Ops/sec | LF Efficiency | Malloc Eff | LF Scale | Speedup\n");
    printf("--------|------------------|----------------|---------------|------------|----------|----------\n");

    static double baseline_lockfree_efficiency = 0.0;
    const char *max_threads_env = getenv("SINGLE_ARENA_PERF_MAX_THREADS");
    const size_t MAX_THREADS = perf_config_get_thread_count(
        (max_threads_env != nullptr) ? "SINGLE_ARENA_PERF_MAX_THREADS" : "SINGLE_BLOCK_PERF_MAX_THREADS",
        16,
        64);

    perf_report report("LockfreeSingleArenaMemoryResourcePerformanceTests", "ThreadScalabilitySensitivityAnalysis");

    for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
    {
        size_t total_allocation_operations = num_threads * PERF_ALLOCATIONS_PER_THREAD;
        size_t total_deallocation_operations = num_threads * PERF_ALLOCATIONS_PER_THREAD * PERF_REPETITIONS;
        size_t total_operations = total_allocation_operations + total_deallocation_operations;

        // ===== Test Lockfree Allocator =====
        start_allocations = false;
        exit_thread = false;

        lockfree_single_arena_resource_perf resource(buffer, BUFFER_SIZE, minstd::pmr::test::os_abstractions::get_cpu_count());

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

        size_t total_allocation_failures = 0;
        for (size_t i = 0; i < num_threads; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
            total_allocation_failures += args[i].allocation_failures;
        }

        if (total_allocation_failures > 0)
        {
            printf("\n[%zu Threads] Lockfree allocation failures caught: %zu\n", num_threads, total_allocation_failures);
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
