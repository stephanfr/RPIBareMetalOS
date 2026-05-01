// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>

#include <__memory_resource/lockfree_static_arena_resource.h>

#include <array>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <random>

namespace
{
    constexpr size_t BUFFER_SIZE = 128 * 1048576; // 128 MB
    alignas(64) uint8_t buffer[BUFFER_SIZE];

    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t SOAK_DURATION_SECONDS = 15; // 15 seconds soak test

    struct alignas(64) soak_thread_arguments
    {
        minstd::pmr::lockfree_static_arena_resource *mem_resource;
        uint64_t rng_seed;
        minstd::atomic<bool>* keep_running;
        size_t total_allocations = 0;
        size_t total_deallocations = 0;
    };

    minstd::atomic<bool> start_allocations = false;
    minstd::atomic<bool> run_soak_test = true;

    void *soak_allocator_thread(void *arg)
    {
        soak_thread_arguments *args = static_cast<soak_thread_arguments *>(arg);

        while (!start_allocations.load(minstd::memory_order_acquire))
        {
            // wait
        }

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));
        
        constexpr size_t MAX_LIVE_ALLOCS = 1000;
        minstd::array<void*, MAX_LIVE_ALLOCS> live_allocs = {nullptr};
        size_t num_live = 0;

        while (args->keep_running->load(minstd::memory_order_acquire))
        {
            if (num_live < MAX_LIVE_ALLOCS && (num_live == 0 || (rng() % 100) < 60)) // Bias slightly toward allocation
            {
                size_t size = (rng() % 2048) + 16;
                void* p = args->mem_resource->allocate(size, 64);
                if (p)
                {
                    *reinterpret_cast<volatile uint8_t*>(p) = 0xFF; // Write an explicit byte to ensure valid page
                    live_allocs[num_live++] = p;
                    args->total_allocations++;
                }
            }
            else if (num_live > 0)
            {
                size_t index = rng() % num_live;
                void* p = live_allocs[index];
                args->mem_resource->deallocate(p, 0, 64);
                live_allocs[index] = live_allocs[--num_live];
                args->total_deallocations++;
            }
        }
        
        // Clean up remaining allocations
        for (size_t i = 0; i < num_live; i++)
        {
            args->mem_resource->deallocate(live_allocs[i], 0, 64);
            args->total_deallocations++;
        }

        return nullptr;
    }
}

TEST_GROUP(lockfree_static_arena_resource_SoakTests)
{
    void setup() {}
    void teardown() {}
};

TEST(lockfree_static_arena_resource_SoakTests, ExtendedMultithreadedSoak)
{
    minstd::pmr::lockfree_static_arena_resource static_resource(buffer, sizeof(buffer));

    pthread_t threads[THREAD_COUNT];
    soak_thread_arguments args[THREAD_COUNT];
    
    run_soak_test.store(true, minstd::memory_order_release);
    start_allocations.store(false, minstd::memory_order_release);

    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        args[i].mem_resource = &static_resource;
        args[i].rng_seed = 1000 + i;
        args[i].keep_running = &run_soak_test;
        args[i].total_allocations = 0;
        args[i].total_deallocations = 0;
        pthread_create(&threads[i], nullptr, soak_allocator_thread, &args[i]);
    }

    start_allocations.store(true, minstd::memory_order_release);

    // Soak...
    sleep(SOAK_DURATION_SECONDS);

    // Conclude
    run_soak_test.store(false, minstd::memory_order_release);

    size_t total_allocs = 0;
    size_t total_deallocs = 0;

    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threads[i], nullptr);
        total_allocs += args[i].total_allocations;
        total_deallocs += args[i].total_deallocations;
    }

    printf("\nLockfree Static Arena Resource Soak Test Complete:\n");
    printf("  Duration:          %zu seconds\n", SOAK_DURATION_SECONDS);
    printf("  Total Threads:     %zu\n", THREAD_COUNT);
    printf("  Total Allocations: %zu\n", total_allocs);
    printf("  Total Deallocs:    %zu\n", total_deallocs);
    CHECK(true);
}
