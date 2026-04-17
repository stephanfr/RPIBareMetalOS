// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/lockfree_composite_single_arena_resource.h>
#include <__memory_resource/lockfree_single_arena_resource.h>
#include <__memory_resource/__extensions/lockfree_single_arena_resource_extended_statistics.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>

#include <sched.h>
#include <stdio.h>
#include <unistd.h>

extern "C" double sqrt(double);
extern "C" double log(double);
extern "C" double cos(double);
extern "C" double exp(double);

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (LockfreeCompositeSingleArenaResourcePerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 2000;
    constexpr size_t MAX_ALLOCATION_SIZE = 16384;

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

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char* buffer = new char[BUFFER_SIZE]();

    minstd::atomic<bool> start_allocations = false;

    using lockfree_composite_single_arena_resource_perf = minstd::pmr::lockfree_composite_single_arena_resource<
        3072,
        256,
        2048,
        32,
        512,
        false,
        32 * 1024 * 1024,
        5>;

    size_t get_number_of_arenas()
    {
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

        if (cpu_count < 1)
        {
            return 1;
        }

        return static_cast<size_t>(cpu_count);
    }

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, NUM_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deleted_element = {false};
        size_t repetitions = 0;
    };

    void *repeated_allocation_deallocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        while (!start_allocations)
        {
            sched_yield();
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t block_size = lognormal_sample(rng);
            if (block_size > MAX_ALLOCATION_SIZE) block_size = MAX_ALLOCATION_SIZE;

            void *ptr = args->mem_resource->allocate(block_size);

            if (ptr == nullptr)
            {
                printf("thread got nullptr\n");
                return nullptr;
            }

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = block_size;
        }

        for (size_t j = 0; j < args->repetitions; j++)
        {
            for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);

                size_t block_size = lognormal_sample(rng);
                if (block_size > MAX_ALLOCATION_SIZE) block_size = MAX_ALLOCATION_SIZE;

                void *ptr = args->mem_resource->allocate(block_size);

                if (ptr == nullptr)
                {
                    printf("thread got nullptr\n");
                    return nullptr;
                }

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

    TEST(LockfreeCompositeSingleArenaResourcePerformanceTests, ThreadScalabilitySensitivityAnalysis)
    {
        printf("\n=== Thread Scalability Analysis: Composite vs Lockfree vs Malloc/Free ===\n");
        printf("Threads | Composite Ops/sec | Lockfree Ops/sec | Malloc Ops/sec | CP Eff | LF Eff | Malloc Eff | CP Scale | LF Scale\n");
        printf("--------|-------------------|---------------------|----------------|--------|--------|------------|----------|----------\n");

        static double baseline_composite_efficiency = 0.0;
        static double baseline_lockfree_efficiency = 0.0;
        constexpr size_t MAX_THREADS = 16;
        constexpr size_t REPETITIONS = 500;

        for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
        {
            size_t total_allocation_operations = num_threads * NUM_ALLOCATIONS_PER_THREAD;
            size_t total_deallocation_operations = num_threads * NUM_ALLOCATIONS_PER_THREAD * REPETITIONS;
            size_t total_operations = total_allocation_operations + total_deallocation_operations;

            start_allocations = false;

            allocator_thread_arguments args[MAX_THREADS];
            pthread_t threads[MAX_THREADS];

            double composite_total_ops_per_second = 0.0;
            double composite_efficiency = 0.0;
            double lockfree_total_ops_per_second = 0.0;
            double lockfree_efficiency = 0.0;
            double malloc_total_ops_per_second = 0.0;
            double malloc_efficiency = 0.0;
            double composite_elapsed_time = 0.0;
            double lockfree_elapsed_time = 0.0;
            double malloc_elapsed_time = 0.0;

            {
                lockfree_composite_single_arena_resource_perf composite_resource(buffer, BUFFER_SIZE, get_number_of_arenas());

                for (size_t i = 0; i < num_threads; i++)
                {
                    args[i].mem_resource = &composite_resource;
                    args[i].rng_seed = i + 1000 + num_threads * 100;
                    args[i].repetitions = REPETITIONS;

                    args[i].pointers_allocated.fill(nullptr);
                    args[i].sizes_allocated.fill(0);
                    args[i].deleted_element.fill(false);

                    CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
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

                composite_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                              (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

                composite_total_ops_per_second = total_operations / composite_elapsed_time;
                composite_efficiency = composite_total_ops_per_second / num_threads;
            }

            start_allocations = false;

            {
                using composite_large_resource_type = minstd::pmr::lockfree_single_arena_resource_impl<
                    minstd::pmr::platform::default_interrupt_policy,
                    minstd::pmr::platform::default_platform_provider,
                    32 * 1024 * 1024,
                    5,
             128>;

                composite_large_resource_type lockfree_resource(buffer, BUFFER_SIZE);

                for (size_t i = 0; i < num_threads; i++)
                {
                    args[i].mem_resource = &lockfree_resource;
                    args[i].rng_seed = i + 2000 + num_threads * 100;
                    args[i].repetitions = REPETITIONS;

                    args[i].pointers_allocated.fill(nullptr);
                    args[i].sizes_allocated.fill(0);
                    args[i].deleted_element.fill(false);

                    CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
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

                lockfree_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                                 (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

                lockfree_total_ops_per_second = total_operations / lockfree_elapsed_time;
                lockfree_efficiency = lockfree_total_ops_per_second / num_threads;
            }

            start_allocations = false;

            {
                minstd::pmr::malloc_free_wrapper_memory_resource malloc_resource(nullptr);

                for (size_t i = 0; i < num_threads; i++)
                {
                    args[i].mem_resource = &malloc_resource;
                    args[i].rng_seed = i + 3000 + num_threads * 100;
                    args[i].repetitions = REPETITIONS;

                    args[i].pointers_allocated.fill(nullptr);
                    args[i].sizes_allocated.fill(0);
                    args[i].deleted_element.fill(false);

                    CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
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

                malloc_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                            (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

                malloc_total_ops_per_second = total_operations / malloc_elapsed_time;
                malloc_efficiency = malloc_total_ops_per_second / num_threads;
            }

            if (num_threads == 1)
            {
                baseline_composite_efficiency = composite_efficiency;
                baseline_lockfree_efficiency = lockfree_efficiency;
            }

            double composite_scalability = baseline_composite_efficiency > 0
                ? (composite_efficiency / baseline_composite_efficiency)
                : 1.0;
            double lockfree_scalability = baseline_lockfree_efficiency > 0
                ? (lockfree_efficiency / baseline_lockfree_efficiency)
                : 1.0;

            printf("  %2zu   | %15.0f | %16.0f | %12.0f | %6.0f | %6.0f | %10.0f | %8.2fx | %8.2fx\n",
                   num_threads,
                   composite_total_ops_per_second,
                   lockfree_total_ops_per_second,
                   malloc_total_ops_per_second,
                   composite_efficiency,
                   lockfree_efficiency,
                   malloc_efficiency,
                   composite_scalability,
                   lockfree_scalability);

            CHECK(composite_elapsed_time > 0);
            CHECK(lockfree_elapsed_time > 0);
            CHECK(malloc_elapsed_time > 0);
            CHECK(composite_total_ops_per_second > 0);
            CHECK(lockfree_total_ops_per_second > 0);
            CHECK(malloc_total_ops_per_second > 0);

            usleep(100000); // 100ms
        }

        printf("\nNotes:\n");
        printf("- Ops/sec: Total allocation + deallocation operations per second\n");
        printf("- Efficiency: Total operations per second per thread\n");
        printf("- CP Scale: Composite efficiency relative to single-thread baseline\n");
        printf("- LF Scale: Lockfree efficiency relative to single-thread baseline\n");
        printf("- Each thread performs %zu initial allocations + %zu repetitions\n",
               NUM_ALLOCATIONS_PER_THREAD, REPETITIONS);
        printf("- Allocation sizes: lognormal(5.4, 1.2) distribution, clamped to [1, %zu] bytes\n",
               MAX_ALLOCATION_SIZE);
         printf("- Composite settings: threshold=3072B, element=256B, arenas<=32, blocks<=512, stats=off, max_bin=32MB, max_waste=5%%\n");
        printf("- Composite routes allocations <= 3072 bytes to fixed_size pool\n");
        printf("- All allocators tested with identical workloads for fair comparison\n");
    }
}
