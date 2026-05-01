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

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 5000;

    constexpr size_t PERF_ALLOCATIONS_PER_THREAD = 2000;
    constexpr size_t PERF_MAX_ALLOCATION_SIZE = 16384;
    constexpr size_t PERF_REPETITIONS = 500;

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char *buffer = new char[BUFFER_SIZE]();

    minstd::atomic<bool> start_allocations = false;
    minstd::atomic<bool> exit_thread = false;
    minstd::atomic<bool> correctness_allocation_failed = false;

    

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

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource = nullptr;
        uint64_t rng_seed = 0;
        bool reduced_pressure_for_correctness = false;
        minstd::array<void *, NUM_ALLOCATIONS_PER_THREAD> pointers_allocated;
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes_allocated;
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deleted_element;
        size_t repetitions = 0;
        size_t allocation_failures = 0;

        allocator_thread_arguments()
        {
            pointers_allocated.fill(nullptr);
            sizes_allocated.fill(0);
            deleted_element.fill(false);
        }
    };

    struct perf_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource = nullptr;
        uint64_t rng_seed = 0;
        minstd::array<void *, PERF_ALLOCATIONS_PER_THREAD> pointers_allocated;
        minstd::array<size_t, PERF_ALLOCATIONS_PER_THREAD> sizes_allocated;
        size_t repetitions = 0;
        size_t allocation_failures = 0;

        perf_thread_arguments()
        {
            pointers_allocated.fill(nullptr);
            sizes_allocated.fill(0);
        }
    };

    void *allocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes;
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deallocate_operation;
        deallocate_operation.fill(false);
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> deallocation_index;
        deallocation_index.fill(0);

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->reduced_pressure_for_correctness)
            {
                sizes[i] = 256 + (rng() % 7000);
            }
            else
            {
                sizes[i] = 256 + (rng() % 18000);
            }
        }

        for (size_t i = 50; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            deallocate_operation[i] = ((rng() % 4) == 0);

            if (deallocate_operation[i])
            {
                deallocation_index[i] = rng() % (i + 1);
            }
        }

        while (!start_allocations)
        {
            // spin wait
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            void *ptr = nullptr;
            do
            {
                ptr = args->mem_resource->allocate(sizes[i]);
                if (ptr == nullptr)
                {
                    args->allocation_failures++;
                }
            } while (ptr == nullptr);

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = sizes[i];

            if (deallocate_operation[i])
            {
                if (!args->deleted_element[deallocation_index[i]])
                {
                    args->mem_resource->deallocate(args->pointers_allocated[deallocation_index[i]], args->sizes_allocated[deallocation_index[i]]);

                    args->deleted_element[deallocation_index[i]] = true;
                }
            }
        }

        return nullptr;
    }

    void *deallocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        while (!start_allocations)
        {
            // spin wait
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            if ((args->pointers_allocated[i] != nullptr) && !args->deleted_element[i])
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    // ---- Interrupt robustness scaffolding ------------------------------------------------

    static volatile sig_atomic_t s_intr_signal_count = 0;
    static volatile sig_atomic_t s_intr_nested_count = 0;
    static thread_local volatile sig_atomic_t s_intr_pending_ops = 0;

    static inline bool process_pending_intr_work(minstd::pmr::memory_resource *resource)
    {
        sig_atomic_t pending = s_intr_pending_ops;
        if (pending <= 0)
        {
            return false;
        }

        s_intr_pending_ops = 0;
        if (pending > 32)
        {
            pending = 32;
        }

        for (sig_atomic_t i = 0; i < pending; ++i)
        {
            void *p = guarded_allocate(resource, 16);
            if (p != nullptr)
            {
                guarded_deallocate(resource, p, 16);
                __atomic_add_fetch(&s_intr_nested_count, 1, __ATOMIC_SEQ_CST);
            }
        }

        return true;
    }

    static inline void drain_pending_intr_work(minstd::pmr::memory_resource *resource)
    {
        while (process_pending_intr_work(resource))
        {
        }
    }

    enum soak_worker_marker : uint32_t
    {
        MARK_LOOP_TOP = 1,
        MARK_PENDING_START,
        MARK_PENDING_DONE,
        MARK_PHASE_READY,
        MARK_DRAIN_IDLE,
        MARK_ALLOC_CALL,
        MARK_ALLOC_OK,
        MARK_ALLOC_FAIL,
        MARK_DEALLOC_CALL,
        MARK_DEALLOC_DONE,
        MARK_EMPTY_YIELD,
        MARK_SHUTDOWN_PENDING,
        MARK_SHUTDOWN_DEALLOC_CALL,
        MARK_SHUTDOWN_DEALLOC_DONE,
        MARK_THREAD_EXIT
    };

}


// clang-format off

TEST_GROUP(LockfreeSingleArenaMemoryResourceMultithreadCorrectnessTests)
{
};

TEST(LockfreeSingleArenaMemoryResourceMultithreadCorrectnessTests, MultiThreadTest)
{
    constexpr size_t NUM_THREADS = 16;

    start_allocations = false;
    correctness_allocation_failed.store(false, minstd::memory_order_release);

    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE, minstd::pmr::test::os_abstractions::get_cpu_count());

    allocator_thread_arguments args[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &resource;
        args[i].rng_seed = i + 333;
        args[i].reduced_pressure_for_correctness = true;

        CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
    }

    sleep(1);

    start_allocations = true;

    timespec start_time{};
    timespec end_time{};
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    CHECK_FALSE(correctness_allocation_failed.load(minstd::memory_order_acquire));

    double duration = (end_time.tv_sec - start_time.tv_sec) +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Lockfree Single Arena Resource Multithread Tests Duration: %f\n", duration);

    size_t total_number_of_allocations = 0;
    size_t total_number_of_bytes_allocated = 0;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        for (size_t j = 0; j < NUM_ALLOCATIONS_PER_THREAD; j++)
        {
            if (args[i].pointers_allocated[j] == nullptr)
            {
                break;
            }

            if (!args[i].deleted_element[j])
            {
                auto alloc_info = resource.get_allocation_info(args[i].pointers_allocated[j]);

                if (alloc_info.state == lockfree_single_arena_resource_with_stats::allocation_state::INVALID)
                {
                    printf("Invalid allocation info\n");
                }

                CHECK(alloc_info.state != lockfree_single_arena_resource_with_stats::allocation_state::INVALID);

                total_number_of_allocations++;
                total_number_of_bytes_allocated += args[i].sizes_allocated[j];
            }
        }
    }

    CHECK_EQUAL(total_number_of_allocations, resource.extended_metrics().current_allocated());
    CHECK_EQUAL(total_number_of_bytes_allocated, resource.current_bytes_allocated());

    // Verify each allocation can be looked up via get_allocation_info.
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        for (size_t j = 0; j < NUM_ALLOCATIONS_PER_THREAD; j++)
        {
            if (args[i].pointers_allocated[j] == nullptr)
            {
                break;
            }

            if (!args[i].deleted_element[j])
            {
                auto alloc_info = resource.get_allocation_info(args[i].pointers_allocated[j]);
                CHECK(alloc_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
            }
        }
    }

    // Deallocate all the allocations.
    start_allocations = false;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &resource;
        args[i].rng_seed = i + 333;

        CHECK(pthread_create(&threads[i], NULL, deallocation_thread, (void *)&args[i]) == 0);
    }

    start_allocations = true;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    CHECK_EQUAL(0, resource.extended_metrics().current_allocated());
    CHECK_EQUAL(0, resource.current_bytes_allocated());

    // Again with malloc/free.
    start_allocations = false;

    minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &malloc_free_resource;

        CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
    }

    sleep(1);

    start_allocations = true;

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    duration = (end_time.tv_sec - start_time.tv_sec) +
               (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Malloc Free Resource Multithread Tests Duration: %f\n", duration);
}
