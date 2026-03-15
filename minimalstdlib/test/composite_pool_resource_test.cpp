// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/composite_pool_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>

#include <sched.h>
#include <stdio.h>
#include <unistd.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (CompositePoolResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 1000;

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char* buffer = new char[BUFFER_SIZE]();

    minstd::atomic<bool> start_allocations = false;

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
            size_t block_size = 128 + (rng() % 7000);

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

                size_t block_size = 128 + (rng() % 7000);

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

    TEST(CompositePoolResourceTests, ThreadScalabilitySensitivityAnalysis)
    {
        printf("\n=== Thread Scalability Analysis: Composite Pool vs Malloc/Free Comparison ===\n");
        printf("Threads | Composite Ops/sec | Malloc Ops/sec | CP Efficiency | Malloc Eff | CP Scale | Speedup\n");
        printf("--------|-------------------|----------------|---------------|------------|----------|----------\n");

        static double baseline_composite_efficiency = 0.0;
        constexpr size_t MAX_THREADS = 16;
        constexpr size_t OPERATIONS_PER_THREAD_SENSITIVITY = 1000;

        for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
        {
            start_allocations = false;

            minstd::pmr::composite_pool_resource<> resource(buffer, BUFFER_SIZE, get_number_of_arenas());

            allocator_thread_arguments args[MAX_THREADS];
            pthread_t threads[MAX_THREADS];

            size_t operations_per_thread = OPERATIONS_PER_THREAD_SENSITIVITY;

            for (size_t i = 0; i < num_threads; i++)
            {
                args[i].mem_resource = &resource;
                args[i].rng_seed = i + 1000 + num_threads * 100;
                args[i].repetitions = 100;

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

            double composite_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                          (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

            size_t total_allocation_operations = num_threads * operations_per_thread;
            size_t total_deallocation_operations = num_threads * operations_per_thread * args[0].repetitions;
            size_t total_operations = total_allocation_operations + total_deallocation_operations;

            double composite_total_ops_per_second = total_operations / composite_elapsed_time;
            double composite_efficiency = composite_total_ops_per_second / num_threads;

            start_allocations = false;

            minstd::pmr::malloc_free_wrapper_memory_resource malloc_resource(nullptr);

            for (size_t i = 0; i < num_threads; i++)
            {
                args[i].mem_resource = &malloc_resource;
                args[i].rng_seed = i + 2000 + num_threads * 100;

                args[i].pointers_allocated.fill(nullptr);
                args[i].sizes_allocated.fill(0);
                args[i].deleted_element.fill(false);

                CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
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
                baseline_composite_efficiency = composite_efficiency;
            }

            double composite_scalability = baseline_composite_efficiency > 0
                ? (composite_efficiency / baseline_composite_efficiency)
                : 1.0;
            double speedup = malloc_total_ops_per_second > 0
                ? (composite_total_ops_per_second / malloc_total_ops_per_second)
                : 0.0;

            printf("  %2zu   | %15.0f | %12.0f | %13.0f | %10.0f | %6.2fx | %6.2fx\n",
                   num_threads, composite_total_ops_per_second, malloc_total_ops_per_second,
                   composite_efficiency, malloc_efficiency, composite_scalability, speedup);

            CHECK(composite_elapsed_time > 0);
            CHECK(malloc_elapsed_time > 0);
            CHECK(composite_total_ops_per_second > 0);
            CHECK(malloc_total_ops_per_second > 0);

            usleep(100000); // 100ms
        }

        printf("\nNotes:\n");
        printf("- Ops/sec: Total allocation + deallocation operations per second\n");
        printf("- Efficiency: Total operations per second per thread\n");
        printf("- CP Scale: Composite efficiency relative to single-thread baseline\n");
        printf("- Speedup: Composite vs Malloc performance ratio (>1.0 = composite faster)\n");
        printf("- Each thread performs %zu initial allocations + 100 repetitions\n",
               OPERATIONS_PER_THREAD_SENSITIVITY);
        printf("- Both allocators tested with identical workloads for fair comparison\n");
    }
}
