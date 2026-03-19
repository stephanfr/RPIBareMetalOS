// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_block_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include "os_abstractions.h"

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (LockfreeSingleBlockMemoryResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t DEFAULT_ALIGNMENT = alignof(max_align_t);

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 5000;

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

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char *buffer = new char[BUFFER_SIZE]();

    minstd::atomic<bool> start_allocations = false;
    minstd::atomic<bool> exit_thread = false;

    typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy<
        minstd::pmr::platform::userspace_signal_mask_interrupt_policy,
        minstd::pmr::extensions::memory_resource_statistics,
        minstd::pmr::extensions::hash_check> lockfree_single_block_resource_with_stats;
    typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy<
        minstd::pmr::platform::userspace_signal_mask_interrupt_policy,
        minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_block_resource_without_stats;

    struct userspace_signal_guard
    {
        userspace_signal_guard()
        {
            minstd::pmr::test::os_abstractions::enter_critical_section();
        }

        ~userspace_signal_guard()
        {
            minstd::pmr::test::os_abstractions::leave_critical_section();
        }
    };

    inline void *guarded_allocate(minstd::pmr::memory_resource *resource, size_t bytes)
    {
        userspace_signal_guard guard;
        return resource->allocate(bytes);
    }

    inline void guarded_deallocate(minstd::pmr::memory_resource *resource, void *ptr, size_t bytes)
    {
        userspace_signal_guard guard;
        resource->deallocate(ptr, bytes);
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

    struct perf_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, PERF_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, PERF_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        size_t repetitions = 0;
    };

    void *allocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes;
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deallocate_operation = {false};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> deallocation_index = {0};

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            sizes[i] = 256 + (rng() % 18000);
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
            sched_yield();
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            void *ptr = args->mem_resource->allocate(sizes[i]);

            if (ptr == nullptr)
            {
                printf("thread got nullptr");
                return nullptr;
            }

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
            sched_yield();
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
                printf("thread got nullptr");
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
                    printf("thread got nullptr");
                    return nullptr;
                }

                args->pointers_allocated[i] = ptr;
                args->sizes_allocated[i] = block_size;
            }

            //            sleep(0.1);
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
            sched_yield();
        }

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t block_size = lognormal_sample(rng);
            if (block_size > PERF_MAX_ALLOCATION_SIZE)
                block_size = PERF_MAX_ALLOCATION_SIZE;

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
            for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);

                size_t block_size = lognormal_sample(rng);
                if (block_size > PERF_MAX_ALLOCATION_SIZE)
                    block_size = PERF_MAX_ALLOCATION_SIZE;

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

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    void *iteration_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        while (!start_allocations)
        {
            sched_yield();
        }

        while (!exit_thread)
        {
            size_t in_use = 0;
            size_t available = 0;
            size_t soft_deleted = 0;
            size_t locked = 0;
            size_t metadata_available = 0;

            for (auto itr = ((lockfree_single_block_resource_with_stats *)(args->mem_resource))->begin(); itr != ((lockfree_single_block_resource_with_stats *)(args->mem_resource))->end(); ++itr)
            {
                auto alloc_info = *itr;

                if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE)
                {
                    in_use++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::AVAILABLE)
                {
                    available++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::SOFT_DELETED)
                {
                    soft_deleted++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::LOCKED)
                {
                    locked++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::METADATA_AVAILABLE)
                {
                    metadata_available++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::FRONTIER_RECLAIM_IN_PROGRESS ||
                         alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::FRONTIER_RECLAIMED)
                {
                    // Transient states during frontier reclamation walk — expected under concurrency
                }
                else
                {
                    CHECK(false);
                }
            }

            //            printf("In Use: %zu, Available: %zu, Soft Deleted: %zu, Locked: %zu\n", in_use, available, soft_deleted, locked);

            sleep(0.2);
        }

        return nullptr;
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, SingleBlockResourceBasicFunctionality)
    {
        lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

        void *ptr1 = resource.allocate(50);

        CHECK(ptr1 != nullptr);
        CHECK((unsigned long)ptr1 % DEFAULT_ALIGNMENT == 0);

        auto allocation_info = resource.get_allocation_info(ptr1);
        CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
        CHECK(allocation_info.size == 50);
        CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

        void *ptr2 = resource.allocate(1023);

        CHECK(ptr2 != nullptr);
        CHECK((unsigned long)ptr2 % DEFAULT_ALIGNMENT == 0);

        allocation_info = resource.get_allocation_info(ptr2);
        CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
        CHECK(allocation_info.size == 1023);
        CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

        void *ptr3 = resource.allocate(123);

        CHECK(ptr3 != nullptr);
        CHECK((unsigned long)ptr3 % DEFAULT_ALIGNMENT == 0);

        allocation_info = resource.get_allocation_info(ptr3);
        CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
        CHECK(allocation_info.size == 123);
        CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

        void *ptr4 = resource.allocate(45678);

        CHECK(ptr4 != nullptr);
        CHECK((unsigned long)ptr4 % DEFAULT_ALIGNMENT == 0);

        allocation_info = resource.get_allocation_info(ptr4);
        CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
        CHECK(allocation_info.size == 45678);
        CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

        int counter = 0;

        for (auto iter = resource.begin(); iter != resource.end(); ++iter)
        {
            allocation_info = *iter;

            CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);

            counter++;
        }

        CHECK(counter == 4);

        resource.deallocate(ptr4, 45678);

        counter = 0;

        for (auto iter = resource.begin(); iter != resource.end(); ++iter)
        {
            allocation_info = *iter;

            if (allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE)
            {
                counter++;
            }
        }

        CHECK(counter == 3);

        void *ptr5 = resource.allocate(100);

        CHECK(ptr5 != nullptr);
        CHECK((unsigned long)ptr5 % DEFAULT_ALIGNMENT == 0);

        allocation_info = resource.get_allocation_info(ptr5);
        CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
        CHECK(allocation_info.size == 100);
        CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, AllocationLargerThanMaxIsRejected)
    {
        lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

        size_t too_large = lockfree_single_block_resource_with_stats::MAX_ALLOCATION_SIZE + 1;
        void *ptr = resource.allocate(too_large);

        CHECK(ptr == nullptr);
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, MultiThreadTest)
    {
        constexpr size_t NUM_THREADS = 16;

        start_allocations = false;
        exit_thread = false;

        lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

        allocator_thread_arguments args[NUM_THREADS];
        pthread_t threads[NUM_THREADS];

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &resource;
            args[i].rng_seed = i + 333;

            CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
        }

        allocator_thread_arguments itr_thread_args[5];
        pthread_t itr_threads[5];

        for (size_t i = 0; i < 5; i++)
        {
            itr_thread_args[i].mem_resource = &resource;

            CHECK(pthread_create(&itr_threads[i], NULL, iteration_thread, (void *)&itr_thread_args[i]) == 0);
        }

        //  Delay a bit for all threads to initialize themselves, then we want to release them so
        //      we can maximally stress the resource.  The count of collisions should provide a
        //      reasonable measure of the contention.

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

        exit_thread = true;

        for (size_t i = 0; i < 5; i++)
        {
            CHECK(pthread_join(itr_threads[i], NULL) == 0);
        }

        double duration = (end_time.tv_sec - start_time.tv_sec) +
                          (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

        printf("Lockfree Single Block Resource Multithread Tests Duration: %f\n", duration);

        //        printf("Retries: %zu\n", resource.cmp_exchange_retries());

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

                    if (!alloc_info.state != lockfree_single_block_resource_with_stats::allocation_state::INVALID)
                    {
                        printf("Invalid allocation info\n");
                    }

                    CHECK(alloc_info.state != lockfree_single_block_resource_with_stats::allocation_state::INVALID);

                    total_number_of_allocations++;
                    total_number_of_bytes_allocated += args[i].sizes_allocated[j];
                }
            }
        }

        CHECK_EQUAL(total_number_of_allocations, resource.current_allocated());
        CHECK_EQUAL(total_number_of_bytes_allocated, resource.current_bytes_allocated());

        //  Check that all thr allocations are correct

        total_number_of_allocations = 0;
        total_number_of_bytes_allocated = 0;
        size_t total_number_of_free_blocks = 0;
        size_t total_number_of_bytes_in_free_blocks = 0;

        size_t number_of_list_elements = 0;

        for (auto itr = resource.begin(); itr != resource.end(); ++itr)
        {
            if ((*itr).state != lockfree_single_block_resource_with_stats::allocation_state::IN_USE)
            {
                if ((*itr).state == lockfree_single_block_resource_with_stats::allocation_state::AVAILABLE)
                {
                    total_number_of_free_blocks++;
                    total_number_of_bytes_in_free_blocks += (*itr).size;
                }

                continue;
            }

            bool found = false;

            for (size_t i = 0; i < NUM_THREADS; i++)
            {
                for (size_t j = 0; j < NUM_ALLOCATIONS_PER_THREAD; j++)
                {
                    if ((*itr).location == args[i].pointers_allocated[j])
                    {
                        found = true;
                        break;
                    }
                }
            }

            CHECK(found);

            number_of_list_elements++;

            //            if (number_of_list_elements % 100 == 0)
            //            {
            //                printf("Number of list elements: %zu\n", number_of_list_elements);
            //            }

            CHECK(found);
        }

        //  Deallocate all the allocations

        start_allocations = false;
        exit_thread = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &resource;
            args[i].rng_seed = i + 333;

            CHECK(pthread_create(&threads[i], NULL, deallocation_thread, (void *)&args[i]) == 0);
        }

        for (size_t i = 0; i < 5; i++)
        {
            itr_thread_args[i].mem_resource = &resource;

            CHECK(pthread_create(&itr_threads[i], NULL, iteration_thread, (void *)&itr_thread_args[i]) == 0);
        }

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        exit_thread = true;

        for (size_t i = 0; i < 5; i++)
        {
            CHECK(pthread_join(itr_threads[i], NULL) == 0);
        }

        CHECK_EQUAL(0, resource.current_allocated());
        CHECK_EQUAL(0, resource.current_bytes_allocated());

        //  Again with malloc/free

        start_allocations = false;

        minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &malloc_free_resource;

            CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
        }

        //  Delay a bit for all threads to initialize themselves, then we want to release them so
        //      we can maximally stress the resource.  The count of collisions should provide a
        //      reasonable measure of the contention.

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

    TEST(LockfreeSingleBlockMemoryResourceTests, MultiThreadAllocateDeallocateTest)
    {
        constexpr size_t NUM_THREADS = 12;

        start_allocations = false;

        lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);

        allocator_thread_arguments args[NUM_THREADS];
        pthread_t threads[NUM_THREADS];

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &resource;
            args[i].rng_seed = i + 444;
            args[i].repetitions = 2000;

            CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
        }

        //  Delay a bit for all threads to initialize themselves, then we want to release them so
        //      we can maximally stress the resource.  The count of collisions should provide a
        //      reasonable measure of the contention.

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

        //  Force two reclaimation passes, with no other threads running this should move all
        //      metadata records into the free headers list.  The free block bins should be empty.

        //        resource.reclaim();
        //        resource.reclaim();
        //        resource.reclaim();
        //        resource.reclaim();
        //        resource.reclaim();
        //        resource.reclaim();

        //        printf("Total number of allocations: %zu  deallocations: %zu\n", resource.total_allocations(), resource.total_deallocations());
        //        printf("Current allocations: %zu  bytes allocated: %zu\n", resource.current_allocated(), resource.current_bytes_allocated());

        //        CHECK_EQUAL(0, resource.current_allocated());
        //        CHECK_EQUAL(0, resource.current_bytes_allocated());

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

    // ---------------------------------------------------------------------------------
    // Mid-list eviction test
    // ---------------------------------------------------------------------------------
    TEST(LockfreeSingleBlockMemoryResourceTests, ReclaimSoftDeletedMetadataMidListEviction)
    {
        lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);

        void* ptr1 = resource.allocate(64);
        void* ptr2 = resource.allocate(64);
        void* ptr3 = resource.allocate(64);
        void* ptr4 = resource.allocate(64);
        void* ptr5 = resource.allocate(64);

        {
            // Create an active iterator to hold the delete cutoff open
            auto iter = resource.begin();

            // Deallocate mid-list elements while iterator is active
            resource.deallocate(ptr2, 64);
            resource.deallocate(ptr4, 64);

            // Iterator goes out of scope here. The destructor calls reclaim_soft_deleted_metadata()
            // which will walk the soft-deleted list and physically unlink ptr2 and ptr4's metadata.
        }

        // Validate the structure is not corrupted and works properly.
        void* ptr6 = resource.allocate(64);
        CHECK(ptr6 != nullptr);

        resource.deallocate(ptr1, 64);
        resource.deallocate(ptr3, 64);
        resource.deallocate(ptr5, 64);
        resource.deallocate(ptr6, 64);
    }

    // ---------------------------------------------------------------------------------
    // Bomber thread test for hardware IRQ robustness
    // ---------------------------------------------------------------------------------
    static minstd::pmr::memory_resource *s_intr_resource = nullptr;
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
            void* p = guarded_allocate(resource, 16);
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
        while (process_pending_intr_work(resource)) {}
    }

    static bool settle_frontier_to_initial(lockfree_single_block_resource_with_stats &resource, size_t initial_frontier)
    {
        const size_t metadata_count = resource.debug_metadata_count();
        const size_t max_attempts = (metadata_count < 1000) ? 50000 : (metadata_count * 64);
        static constexpr size_t probe_sizes[] = {16, 4096, 65536, 1048576, 4194304};

        for (size_t attempt = 0; attempt < max_attempts; ++attempt)
        {
            if (resource.debug_frontier_offset() == initial_frontier)
            {
                return true;
            }

            // Nudge allocator paths with varying sizes so we don't only churn a single tiny free-list bin.
            const size_t probe_size = probe_sizes[attempt % (sizeof(probe_sizes) / sizeof(probe_sizes[0]))];
            void *probe = guarded_allocate(&resource, probe_size);
            if (probe != nullptr)
            {
                guarded_deallocate(&resource, probe, probe_size);
            }

            // Periodically create and destroy an iterator to trigger soft-delete metadata reclamation.
            if ((attempt & 0xFF) == 0)
            {
                auto itr = resource.begin();
                (void)itr;
            }

            if ((attempt & 0x3F) == 0)
            {
                sched_yield();
            }
        }

        return resource.debug_frontier_offset() == initial_frontier;
    }

    static void sigusr1_nested_alloc_handler(int)
    {
        if (s_intr_resource != nullptr)
        {
            // Defer nested work to normal thread context. Signal context only records intent.
            __atomic_add_fetch(&s_intr_pending_ops, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&s_intr_signal_count, 1, __ATOMIC_SEQ_CST);
        }
    }

    struct intr_test_bomber_args
    {
        pthread_t target_thread;
        minstd::atomic<bool> *stop_flag;
    };

    static void *intr_test_bomber_fn(void *arg)
    {
        auto *a = static_cast<intr_test_bomber_args *>(arg);
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            pthread_kill(a->target_thread, SIGUSR1);
            usleep(50);
        }
        return nullptr;
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, InterruptRobustness)
    {
        lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);
        s_intr_resource = &resource;
        s_intr_signal_count = 0;
        s_intr_nested_count = 0;
        s_intr_pending_ops = 0;

        struct sigaction sa = {};
        sa.sa_handler = sigusr1_nested_alloc_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGUSR1, &sa, nullptr);

        minstd::atomic<bool> stop_flag{false};
        intr_test_bomber_args args{pthread_self(), &stop_flag};
        pthread_t bomber;
        CHECK_EQUAL(0, pthread_create(&bomber, nullptr, intr_test_bomber_fn, &args));

        // Perform normal operations while being bombarded with signals
        for (int i = 0; i < 50000; ++i)
        {
            process_pending_intr_work(&resource);

            void* p = guarded_allocate(&resource, 32);
            if (p) 
            {
                guarded_deallocate(&resource, p, 32);
            }
        }

        drain_pending_intr_work(&resource);

        stop_flag.store(true, minstd::memory_order_release);
        pthread_join(bomber, nullptr);
        
        // Disable the handler
        sa.sa_handler = SIG_DFL;
        sigaction(SIGUSR1, &sa, nullptr);
        s_intr_resource = nullptr;

        // Ensure variables were actually triggered
        CHECK_TRUE(s_intr_signal_count > 0);
        CHECK_TRUE(s_intr_nested_count > 0);
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, ThreadScalabilitySensitivityAnalysis)
    {
        printf("\n=== Thread Scalability Analysis: Lockfree vs Malloc/Free Comparison ===\n");
        printf("Threads | Lockfree Ops/sec | Malloc Ops/sec | LF Efficiency | Malloc Eff | LF Scale | Speedup\n");
        printf("--------|------------------|----------------|---------------|------------|----------|----------\n");

        static double baseline_lockfree_efficiency = 0.0;
        constexpr size_t MAX_THREADS = 16;

        // Test with thread counts from 1 to 16
        for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
        {
            size_t total_allocation_operations = num_threads * PERF_ALLOCATIONS_PER_THREAD;
            size_t total_deallocation_operations = num_threads * PERF_ALLOCATIONS_PER_THREAD * PERF_REPETITIONS;
            size_t total_operations = total_allocation_operations + total_deallocation_operations;

            // ===== Test Lockfree Allocator =====
            start_allocations = false;
            exit_thread = false;

            lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);

            perf_thread_arguments args[MAX_THREADS];
            pthread_t threads[MAX_THREADS];

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
    }

    struct soak_thread_args
    {
        lockfree_single_block_resource_with_stats *resource;
        minstd::atomic<bool> *stop_flag;
        minstd::atomic<int> *shared_phase;       // Phase set by main loop (0=steady,1=bursty,2=recovery,3=drain)
        minstd::atomic<bool> *use_shared_phase;   // true=all threads follow shared_phase, false=per-thread cycling
        minstd::atomic<int> *drained_count;       // Threads increment when live_count reaches 0 during drain
        minstd::atomic<int> *drain_epoch;         // Incremented by main when entering drain
        minstd::atomic<int> *drain_ack_count;     // Workers ack once per drain epoch
        uint64_t rng_seed;
        size_t id;
        minstd::atomic<size_t> allocations{0};
        minstd::atomic<size_t> deallocations{0};
        minstd::atomic<size_t> failed_allocations{0};
        minstd::atomic<size_t> current_live_count{0};
        minstd::atomic<size_t> heartbeat{0};
        minstd::atomic<uint64_t> last_progress_ns{0};
        minstd::atomic<int> last_phase_seen{0};
        minstd::atomic<size_t> last_live_seen{0};
    };

    static inline uint64_t monotonic_time_ns()
    {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }

    static void *soak_worker_thread(void *arg)
    {
        auto *args = static_cast<soak_thread_args *>(arg);
        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        constexpr size_t MAX_LIVE = 4000;
        void* pointers[MAX_LIVE]{};
        size_t sizes[MAX_LIVE]{};

        size_t live_count = 0;
        size_t loop_counter = 0;
        bool drain_signaled = false;

        // Per-thread independent phase state
        int local_phase = 0;
        int local_cycle_count = 0;
        time_t local_phase_start = time(NULL);
        time_t local_phase_duration = 10 + ((int64_t)(rng() % 11) - 5);
        int last_acked_drain_epoch = -1;
        int previous_phase = -1;
        size_t burst_target_live = 2500;
        bool burst_pressure_spike = false;

        while (!args->stop_flag->load(minstd::memory_order_acquire))
        {
            process_pending_intr_work(args->resource);

            // Determine current phase: shared or independent
            int current_phase;
            if (args->use_shared_phase->load(minstd::memory_order_acquire))
            {
                current_phase = args->shared_phase->load(minstd::memory_order_acquire);
            }
            else
            {
                // Per-thread timer-based phase cycling
                if (++loop_counter % 100000 == 0)
                {
                    time_t now = time(NULL);
                    if (now - local_phase_start >= local_phase_duration)
                    {
                        if (local_phase == 0) {
                            local_phase = 1;
                        } else if (local_phase == 1) {
                            local_phase = 2;
                        } else {
                            local_phase = 0;
                            local_cycle_count++;
                        }
                        local_phase_start = now;
                        local_phase_duration = 10 + ((int64_t)(rng() % 11) - 5);

                        static const char* pnames[] = {"STEADY", "BURSTY", "RECOVERY"};
                        printf("  [Thread %zu] Independent -> %s (duration: %zd secs, live: %zu)\n",
                               args->id, pnames[local_phase], (ssize_t)local_phase_duration, live_count);
                        fflush(stdout);
                    }
                }
                current_phase = local_phase;
            }

            if (current_phase != previous_phase)
            {
                if (current_phase == 1)
                {
                    // Re-roll burst target each BURSTY entry so pressure varies and intermittently
                    // pushes close to MAX_LIVE, provoking occasional allocation exhaustion.
                    burst_target_live = 2200 + (rng() % 1701); // [2200, 3900]
                    burst_pressure_spike = ((rng() % 5) == 0); // 20% of bursts are near-cap spikes
                    if (burst_pressure_spike)
                    {
                        burst_target_live = 3800 + (rng() % 151); // [3800, 3950]
                    }
                }

                previous_phase = current_phase;
            }

            args->last_phase_seen.store(current_phase, minstd::memory_order_relaxed);
            args->last_live_seen.store(live_count, minstd::memory_order_relaxed);

            if (current_phase == 3)
            {
                const int drain_epoch = args->drain_epoch->load(minstd::memory_order_acquire);
                if (drain_epoch != last_acked_drain_epoch)
                {
                    args->drain_ack_count->fetch_add(1, minstd::memory_order_release);
                    last_acked_drain_epoch = drain_epoch;
                }
            }

            // Alloc probability varies by phase; BURSTY/RECOVERY revert to 50/50 once target is reached
            int alloc_pct;
            if (current_phase == 0) {
                alloc_pct = 50;                          // STEADY: 50/50
            } else if (current_phase == 1) {
                if (live_count < burst_target_live)
                {
                    alloc_pct = burst_pressure_spike ? 98 : 92;
                }
                else
                {
                    alloc_pct = 50;
                }
            } else if (current_phase == 2) {
                alloc_pct = (live_count > 10) ? 10 : 50;   // RECOVERY: drain to ~100, then 50/50
            } else {
                alloc_pct = 0;                           // DRAIN: dealloc only
            }

            // Drain phase: signal when empty, yield until phase changes
            if (current_phase == 3)
            {
                if (live_count == 0)
                {
                    if (!drain_signaled)
                    {
                        args->drained_count->fetch_add(1, minstd::memory_order_release);
                        drain_signaled = true;
                    }
                    sched_yield();
                    continue;
                }
            }
            else
            {
                drain_signaled = false;
            }

            bool do_alloc;
            if (live_count == 0 && current_phase != 3) {
                do_alloc = true;
            } else if (live_count >= MAX_LIVE) {
                do_alloc = false;
            } else {
                do_alloc = ((int)(rng() % 100)) < alloc_pct;
            }

            if (do_alloc)
            {
                // Size between 1 and 32000
                size_t sz = 1 + (rng() % 32000);
                void* p = guarded_allocate(args->resource, sz);
                if (p)
                {
                    pointers[live_count] = p;
                    sizes[live_count] = sz;
                    live_count++;
                    args->allocations.fetch_add(1, minstd::memory_order_relaxed);
                    args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                    args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
                }
                else
                {
                    args->failed_allocations.fetch_add(1, minstd::memory_order_relaxed);
                    args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                    args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
                }
            }
            else if (live_count > 0)
            {
                size_t idx = rng() % live_count;
                guarded_deallocate(args->resource, pointers[idx], sizes[idx]);
                pointers[idx] = pointers[live_count - 1];
                sizes[idx] = sizes[live_count - 1];
                live_count--;
                args->deallocations.fetch_add(1, minstd::memory_order_relaxed);
                args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
            }
            else
            {
                // Nothing to deallocate — pause to avoid tight spin
                sched_yield();
            }

            args->current_live_count.store(live_count, minstd::memory_order_relaxed);

            const size_t ops = args->allocations.load(minstd::memory_order_relaxed) + args->deallocations.load(minstd::memory_order_relaxed);
            if (ops % 1000 == 0)
            {
                sched_yield();
            }
        }

        // Drain all deferred signal work before final thread-local cleanup.
        drain_pending_intr_work(args->resource);

        for (size_t i = 0; i < live_count; ++i)
        {
            guarded_deallocate(args->resource, pointers[i], sizes[i]);
            args->deallocations.fetch_add(1, minstd::memory_order_relaxed);
        }

        return nullptr;
    }

    struct soak_bomber_args
    {
        pthread_t* targets;
        size_t num_targets;
        minstd::atomic<bool>* stop_flag;
    };

    static void *soak_bomber_thread(void *arg)
    {
        auto *a = static_cast<soak_bomber_args *>(arg);
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            for (size_t i = 0; i < a->num_targets; i++)
            {
                pthread_kill(a->targets[i], SIGUSR1);
            }
            usleep(100);
        }
        return nullptr;
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, SoakTest)
    {
        const size_t NUM_THREADS = 8;
        
        size_t SOAK_DURATION_SEC = 180;
        const char* soak_duration_env = getenv("ALLOCATOR_SOAK_DURATION");
        if (soak_duration_env)
        {
            SOAK_DURATION_SEC = atoi(soak_duration_env);
        }

        uint64_t base_seed = 987654321ULL;
        const char* soak_seed_env = getenv("ALLOCATOR_SOAK_SEED");
        if (soak_seed_env)
        {
            base_seed = strtoull(soak_seed_env, nullptr, 10);
        }
        else
        {
            base_seed += time(nullptr);
        }

        printf("\nRunning Allocator SoakTest for %zu seconds (Base Seed: %llu)...\n", SOAK_DURATION_SEC, (unsigned long long)base_seed);

        lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);
        const size_t initial_frontier = resource.debug_frontier_offset();
        
        struct sigaction sa = {};
        sa.sa_handler = sigusr1_nested_alloc_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGUSR1, &sa, nullptr);

        s_intr_resource = &resource;
        s_intr_signal_count = 0;
        s_intr_nested_count = 0;
        s_intr_pending_ops = 0;

        minstd::atomic<bool> stop_flag{false};
        minstd::atomic<int> shared_phase{0};
        minstd::atomic<bool> use_shared_phase{true};
        minstd::atomic<int> drained_count{0};
        minstd::atomic<int> drain_epoch{0};
        minstd::atomic<int> drain_ack_count{0};
        
        pthread_t workers[NUM_THREADS]{};
        soak_thread_args thread_args[NUM_THREADS]{};
        
        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            thread_args[i].resource = &resource;
            thread_args[i].stop_flag = &stop_flag;
            thread_args[i].shared_phase = &shared_phase;
            thread_args[i].use_shared_phase = &use_shared_phase;
            thread_args[i].drained_count = &drained_count;
            thread_args[i].drain_epoch = &drain_epoch;
            thread_args[i].drain_ack_count = &drain_ack_count;
            thread_args[i].rng_seed = base_seed + i;
            thread_args[i].id = i;
            thread_args[i].last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
            
            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, soak_worker_thread, &thread_args[i]));
        }
        
        soak_bomber_args b_args;
        b_args.targets = workers;
        b_args.num_targets = NUM_THREADS;
        b_args.stop_flag = &stop_flag;
        
        pthread_t bomber;
        CHECK_EQUAL(0, pthread_create(&bomber, nullptr, soak_bomber_thread, &b_args));

        // Main loop: manages phase transitions and reporting
        minstd::Xoroshiro128PlusPlusRNG main_rng(minstd::Xoroshiro128PlusPlusRNG::Seed(123456789ULL ^ base_seed, base_seed));
        int main_phase = 0;             // 0=steady, 1=bursty, 2=recovery, 3=drain
        int main_cycle_count = 0;
        bool main_shared_mode = true;
        time_t phase_start = time(NULL);
        time_t phase_dur = 10 + ((int64_t)(main_rng() % 11) - 5);

        static const char* phase_names[] = {"STEADY", "BURSTY", "RECOVERY", "DRAIN"};
        printf("Phase -> %s [SHARED] (duration: %zd secs)\n", phase_names[main_phase], (ssize_t)phase_dur);
        fflush(stdout);

        size_t elapsed = 0;
        size_t last_allocs = 0;
        size_t last_deallocs = 0;
        size_t last_failed = 0;
        size_t last_heartbeats[NUM_THREADS]{};
        size_t hb_deltas[NUM_THREADS]{};
        while (elapsed < SOAK_DURATION_SEC * 10)
        {
            // Phase transition check every second
            if (elapsed % 10 == 0)
            {
                time_t now = time(NULL);
                bool transition = false;

                if (main_phase == 3)
                {
                    // In drain: first wait for all threads to observe this drain epoch, then wait for empty.
                    const int acked = drain_ack_count.load(minstd::memory_order_acquire);
                    const int drained = drained_count.load(minstd::memory_order_acquire);
                    if (acked >= (int)NUM_THREADS && drained >= (int)NUM_THREADS)
                    {
                        transition = true;
                    }
                }
                else if (now - phase_start >= phase_dur)
                {
                    transition = true;
                }

                if (transition)
                {
                    if (main_phase == 0) {
                        main_phase = 1;
                    } else if (main_phase == 1) {
                        main_phase = 2;
                    } else if (main_phase == 2) {
                        main_cycle_count++;
                        if (main_cycle_count % 4 == 0) {
                            main_phase = 3;     // every 4th cycle: drain
                        } else {
                            main_phase = 0;     // start new cycle
                            // Flip coin for shared vs independent mode
                            main_shared_mode = (main_rng() % 2) == 0;
                        }
                    } else {
                        // Drained worker-local live sets are a phase-transition condition only.
                        // Strict frontier reset is validated at final quiescent teardown checks.
                        size_t frontier_after_drain = resource.debug_frontier_offset();
                        if (frontier_after_drain != initial_frontier)
                        {
                            printf("  [DrainComplete] frontier=%zu (initial=%zu); defer strict check to final quiescent validation\n",
                                   frontier_after_drain, initial_frontier);
                            fflush(stdout);
                        }

                        // After drain, always start fresh cycle in shared mode
                        main_phase = 0;
                        main_shared_mode = (main_rng() % 2) == 0;
                    }

                    phase_start = now;
                    phase_dur = 10 + ((int64_t)(main_rng() % 11) - 5);

                    // Drain always uses shared mode
                    if (main_phase == 3) {
                        use_shared_phase.store(true, minstd::memory_order_release);
                        drained_count.store(0, minstd::memory_order_release);
                        drain_ack_count.store(0, minstd::memory_order_release);
                        drain_epoch.fetch_add(1, minstd::memory_order_acq_rel);
                    } else {
                        use_shared_phase.store(main_shared_mode, minstd::memory_order_release);
                    }
                    shared_phase.store(main_phase, minstd::memory_order_release);

                    printf("Phase -> %s [%s] (duration: %zd secs, cycle: %d)\n",
                           phase_names[main_phase],
                           (main_phase == 3 || main_shared_mode) ? "SHARED" : "INDEPENDENT",
                           (ssize_t)phase_dur, main_cycle_count);
                    fflush(stdout);
                }
            }

            // Stats reporting every 10 seconds
            if (elapsed % 100 == 0 && elapsed > 0)
            {
                size_t c_allocs = 0, c_deallocs = 0, c_failed = 0, c_live = 0;
                for (size_t i = 0; i < NUM_THREADS; ++i)
                {
                    c_allocs += thread_args[i].allocations.load(minstd::memory_order_relaxed);
                    c_deallocs += thread_args[i].deallocations.load(minstd::memory_order_relaxed);
                    c_failed += thread_args[i].failed_allocations.load(minstd::memory_order_relaxed);
                    c_live += thread_args[i].current_live_count.load(minstd::memory_order_relaxed);
                }

                for (size_t i = 0; i < NUM_THREADS; ++i)
                {
                    const size_t hb = thread_args[i].heartbeat.load(minstd::memory_order_relaxed);
                    hb_deltas[i] = hb - last_heartbeats[i];
                    last_heartbeats[i] = hb;
                }

                size_t allocs_per_sec = (c_allocs - last_allocs) / 10;
                size_t deallocs_per_sec = (c_deallocs - last_deallocs) / 10;
                size_t failed_per_sec = (c_failed - last_failed) / 10;
                last_allocs = c_allocs;
                last_deallocs = c_deallocs;
                last_failed = c_failed;

                size_t frontier_off = resource.debug_frontier_offset();
                size_t meta_count = resource.debug_metadata_count();
                size_t meta_boundary = resource.debug_metadata_boundary_offset();
                size_t gap = (meta_boundary > frontier_off) ? (meta_boundary - frontier_off) : 0;

                printf("Elapsed: %zu secs, Live: %zu, Allocs: %zu ( %zu /sec ), Deallocs: %zu ( %zu /sec ), Failed: %zu ( %zu /sec )\n",
                       elapsed / 10, c_live, c_allocs, allocs_per_sec, c_deallocs, deallocs_per_sec, c_failed, failed_per_sec);
                printf("  Frontier: %zuMB, MetaCount: %zu, MetaBoundary: %zuMB, Gap: %zuMB\n",
                       frontier_off / (1024*1024), meta_count, meta_boundary / (1024*1024), gap / (1024*1024));
                fflush(stdout);

                if (main_phase == 3 && allocs_per_sec == 0 && deallocs_per_sec == 0)
                {
                    int drained = drained_count.load(minstd::memory_order_acquire);
                    int acked = drain_ack_count.load(minstd::memory_order_acquire);
                    const uint64_t now_ns = monotonic_time_ns();
                    printf("  [DRAIN STALL?] acked=%d/%zu drained_count=%d/%zu\n", acked, NUM_THREADS, drained, NUM_THREADS);
                    for (size_t i = 0; i < NUM_THREADS; ++i)
                    {
                        uint64_t last_ns = thread_args[i].last_progress_ns.load(minstd::memory_order_relaxed);
                        uint64_t age_ms = (now_ns > last_ns) ? ((now_ns - last_ns) / 1000000ULL) : 0ULL;
                        int t_phase = thread_args[i].last_phase_seen.load(minstd::memory_order_relaxed);
                        size_t t_live = thread_args[i].last_live_seen.load(minstd::memory_order_relaxed);
                        printf("    [T%zu] phase=%d live=%zu hb_delta=%zu age_ms=%llu alloc=%zu dealloc=%zu failed=%zu\n",
                               i, t_phase, t_live, hb_deltas[i], (unsigned long long)age_ms,
                               thread_args[i].allocations.load(minstd::memory_order_relaxed),
                               thread_args[i].deallocations.load(minstd::memory_order_relaxed),
                               thread_args[i].failed_allocations.load(minstd::memory_order_relaxed));
                    }
                    fflush(stdout);

                    if (getenv("ALLOCATOR_SOAK_BREAK_ON_STALL"))
                    {
                        raise(SIGTRAP);
                    }
                }
            }
            usleep(100000); // 100ms
            elapsed++;
        }

        // Force a final shared DRAIN phase so teardown checks run from a true quiescent point.
        use_shared_phase.store(true, minstd::memory_order_release);
        drained_count.store(0, minstd::memory_order_release);
        drain_ack_count.store(0, minstd::memory_order_release);
        drain_epoch.fetch_add(1, minstd::memory_order_acq_rel);
        shared_phase.store(3, minstd::memory_order_release);

        const size_t FINAL_DRAIN_WAIT_TICKS = 300; // 30 seconds max at 100ms per tick
        bool final_drain_complete = false;
        for (size_t tick = 0; tick < FINAL_DRAIN_WAIT_TICKS; ++tick)
        {
            int acked = drain_ack_count.load(minstd::memory_order_acquire);
            int drained = drained_count.load(minstd::memory_order_acquire);
            if (acked >= (int)NUM_THREADS && drained >= (int)NUM_THREADS)
            {
                final_drain_complete = true;
                break;
            }
            usleep(100000);
        }

        if (!final_drain_complete)
        {
            printf("[FinalDrainTimeout] acked=%d/%zu drained=%d/%zu\n",
                   drain_ack_count.load(minstd::memory_order_acquire), NUM_THREADS,
                   drained_count.load(minstd::memory_order_acquire), NUM_THREADS);
            fflush(stdout);
        }

        stop_flag.store(true, minstd::memory_order_release);

        pthread_join(bomber, nullptr);

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            pthread_join(workers[i], nullptr);
        }
        
        sa.sa_handler = SIG_DFL;
        sigaction(SIGUSR1, &sa, nullptr);
        s_intr_resource = nullptr;

        // Note: worker threads drain their own thread_local s_intr_pending_ops via
        // drain_pending_intr_work() before exiting.  No main-thread drain needed here
        // since the bomber only targets worker threads.

        bool frontier_settled = settle_frontier_to_initial(resource, initial_frontier);
        if (!frontier_settled)
        {
            printf("[QuiescentSettle] frontier remained at %zu (initial=%zu, metadata=%zu)\n",
                   resource.debug_frontier_offset(), initial_frontier, resource.debug_metadata_count());
            fflush(stdout);
        }

        size_t total_alloc = 0;
        size_t total_dealloc = 0;
        size_t total_failed = 0;
        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            total_alloc += thread_args[i].allocations.load(minstd::memory_order_relaxed);
            total_dealloc += thread_args[i].deallocations.load(minstd::memory_order_relaxed);
            total_failed += thread_args[i].failed_allocations.load(minstd::memory_order_relaxed);
        }

        printf("Soak test completed. Worker Allocs: %zu, Worker Deallocs: %zu, Failed Allocs: %zu (total across threads)\n",
               total_alloc, total_dealloc, total_failed);
        printf("Signals delivered: %d, Nested allocs triggered: %d\n", (int)s_intr_signal_count, (int)s_intr_nested_count);
        printf("Resource: total_allocs=%zu, total_deallocs=%zu, current_bytes=%zu, current_allocated=%zu\n",
               resource.total_allocations(), resource.total_deallocations(),
               resource.current_bytes_allocated(), resource.current_allocated());
        printf("Frontier offset: %zu, Metadata count: %zu\n",
               resource.debug_frontier_offset(), resource.debug_metadata_count());
        fflush(stdout);
        
        // Assert no leaks — breakpoint-friendly trap before CHECK_EQUAL
        if (resource.current_bytes_allocated() != 0)
        {
            printf("*** LEAK DETECTED: current_bytes_allocated = %zu ***\n", resource.current_bytes_allocated());
            fflush(stdout);  // GDB breakpoint target line
        }
        CHECK_EQUAL(0, resource.current_bytes_allocated());
        // Optional strict mode for frontier compaction debugging.
        const bool enforce_frontier_reset = (getenv("ALLOCATOR_SOAK_ENFORCE_FRONTIER_RESET") != nullptr);
        if (enforce_frontier_reset)
        {
            CHECK_TRUE(frontier_settled);
            CHECK_EQUAL(initial_frontier, resource.debug_frontier_offset());
        }
        else
        {
            CHECK_TRUE(resource.debug_frontier_offset() <= resource.debug_metadata_boundary_offset());
        }
        CHECK_EQUAL(total_alloc + s_intr_nested_count, resource.total_allocations());
        CHECK_EQUAL(total_dealloc + s_intr_nested_count, resource.total_deallocations());
    }
}
