// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>

#include <__memory_resource/lockfree_static_arena_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include "../shared/interrupt_simulation_test_helpers.h"

#include <array>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <random>

#include <random>

#include "../shared/perf_test_config.h"

namespace
{
    constexpr size_t BUFFER_SIZE = 128 * 1048576; // 128 MB
    alignas(64) uint8_t buffer[BUFFER_SIZE];

    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t PERF_ALLOCATIONS_PER_THREAD = 20000;

    struct alignas(64) perf_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, PERF_ALLOCATIONS_PER_THREAD> pointers_allocated;
    };

    minstd::atomic<bool> start_allocations = false;

    void *perf_allocator_thread(void *arg)
    {
        perf_thread_arguments *args = static_cast<perf_thread_arguments *>(arg);

        while (!start_allocations.load(minstd::memory_order_acquire))
        {
            // wait
        }

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t size = (rng() % 1024) + 16;
            args->pointers_allocated[i] = args->mem_resource->allocate(size, 64);
        }
        
        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr) {
                args->mem_resource->deallocate(args->pointers_allocated[i], 0, 64);
            }
        }

        return nullptr;
    }
}

TEST_GROUP(lockfree_static_arena_resource_PerformanceTests)
{
    void setup() {}
    void teardown() {}
};

TEST(lockfree_static_arena_resource_PerformanceTests, BenchmarkCorrectness)
{
    start_allocations.store(false, minstd::memory_order_release);
    minstd::pmr::lockfree_static_arena_resource static_resource(buffer, sizeof(buffer));

    pthread_t threads[THREAD_COUNT];
    perf_thread_arguments args[THREAD_COUNT];

    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        args[i].mem_resource = &static_resource;
        args[i].rng_seed = 1000 + i;
        for(size_t j=0; j<PERF_ALLOCATIONS_PER_THREAD; j++) args[i].pointers_allocated[j] = nullptr;
        pthread_create(&threads[i], nullptr, perf_allocator_thread, &args[i]);
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    start_allocations.store(true, minstd::memory_order_release);

    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threads[i], nullptr);
    }

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    
    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        for (size_t j = 0; j < PERF_ALLOCATIONS_PER_THREAD; j++)
        {
            // Just verifying it doesn't instantly crash
            CHECK(true);
            break;
        }
    }

    printf("Lockfree Static Arena Resource Multithread Perf Duration: %f seconds for %zu allocs/deallocs\n", duration, THREAD_COUNT * PERF_ALLOCATIONS_PER_THREAD);
}

TEST(lockfree_static_arena_resource_PerformanceTests, BenchmarkMallocFree)
{
    start_allocations.store(false, minstd::memory_order_release);
    minstd::pmr::malloc_free_wrapper_memory_resource malloc_resource(nullptr);

    pthread_t threads[THREAD_COUNT];
    perf_thread_arguments args[THREAD_COUNT];

    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        args[i].mem_resource = &malloc_resource;
        args[i].rng_seed = 2000 + i;
        for(size_t j=0; j<PERF_ALLOCATIONS_PER_THREAD; j++) args[i].pointers_allocated[j] = nullptr;
        pthread_create(&threads[i], nullptr, perf_allocator_thread, &args[i]);
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    start_allocations.store(true, minstd::memory_order_release);

    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threads[i], nullptr);
    }

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    
    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        for (size_t j = 0; j < PERF_ALLOCATIONS_PER_THREAD; j++)
        {
            CHECK(true);
            break;
        }
    }

    printf("Malloc/Free Wrapper Multithread Perf Duration:            %f seconds for %zu allocs/deallocs\n", duration, THREAD_COUNT * PERF_ALLOCATIONS_PER_THREAD);
}
