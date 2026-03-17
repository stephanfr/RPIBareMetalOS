// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_block_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

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

    typedef minstd::pmr::lockfree_single_block_resource<minstd::pmr::extensions::memory_resource_statistics, minstd::pmr::extensions::hash_check> lockfree_single_block_resource_with_stats;
    typedef minstd::pmr::lockfree_single_block_resource<minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_block_resource_without_stats;

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

    static void sigusr1_nested_alloc_handler(int)
    {
        if (s_intr_resource != nullptr)
        {
            // Triggers a nested operation — matching bare-metal hardware IRQ behavior
            void* p = s_intr_resource->allocate(16);
            if (p != nullptr)
            {
                s_intr_resource->deallocate(p, 16);
                __atomic_add_fetch(&s_intr_nested_count, 1, __ATOMIC_SEQ_CST);
            }
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
            void* p = resource.allocate(32);
            if (p) 
            {
                resource.deallocate(p, 32);
            }
        }

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
        uint64_t rng_seed;
        size_t id;
        size_t allocations = 0;
        size_t deallocations = 0;
        size_t failed_allocations = 0;
    };

    static void *soak_worker_thread(void *arg)
    {
        auto *args = static_cast<soak_thread_args *>(arg);
        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        constexpr size_t MAX_LIVE = 4000;
        void* pointers[MAX_LIVE]{};
        size_t sizes[MAX_LIVE]{};

        size_t live_count = 0;
        int current_phase = 0;       // 0=steady, 1=bursty, 2=recovery, 3=drain
        int cycle_count = 0;          // counts full cycles through phases 0-2
        size_t loop_counter = 0;

        // Timer-based phase transitions: 120 +/- 60 seconds per phase
        time_t phase_start_time = time(NULL);
        time_t phase_duration = 120 + ((int64_t)(rng() % 121) - 60);

        while (!args->stop_flag->load(minstd::memory_order_acquire))
        {
            // Check for phase transition every 100k loops to minimize syscall overhead
            if (++loop_counter % 100000 == 0)
            {
                time_t now = time(NULL);
                if (now - phase_start_time >= phase_duration)
                {
                    if (current_phase == 0) {
                        current_phase = 1;  // steady -> bursty
                    } else if (current_phase == 1) {
                        current_phase = 2;  // bursty -> recovery
                    } else if (current_phase == 2) {
                        cycle_count++;
                        if (cycle_count % 4 == 0) {
                            current_phase = 3;  // every 4th cycle: recovery -> drain
                        } else {
                            current_phase = 0;  // recovery -> steady
                        }
                    } else {
                        current_phase = 0;  // drain -> steady
                    }
                    phase_start_time = now;
                    phase_duration = 120 + ((int64_t)(rng() % 121) - 60);

                    static const char* phase_names[] = {"STEADY", "BURSTY", "RECOVERY", "DRAIN"};
                    printf("  [Thread %zu] Phase -> %s (duration: %zd secs, live: %zu, cycle: %d)\n",
                           args->id, phase_names[current_phase], (ssize_t)phase_duration, live_count, cycle_count);
                    fflush(stdout);
                }
            }

            size_t target_max;
            int alloc_chance;
            if (current_phase == 0) {
                // Steady-state: balanced allocs/deallocs
                target_max = 500;
                alloc_chance = 2;   // 50% allocate
            } else if (current_phase == 1) {
                // Bursty: heavy allocs, few deallocs
                target_max = 3000;
                alloc_chance = 10;  // 90% allocate
            } else if (current_phase == 2) {
                // Recovery: predominantly deletes
                target_max = 10;
                alloc_chance = 10;  // 10% allocate
            } else {
                // Drain: empty the pool entirely
                target_max = 0;
                alloc_chance = 10;
            }

            if (live_count == 0 && target_max == 0)
            {
                sched_yield();
                continue;
            }

            if (live_count == 0 || (live_count < target_max && (rng() % alloc_chance) != 0))
            {
                // Size between 1 and 32000
                size_t sz = 1 + (rng() % 32000);
                void* p = args->resource->allocate(sz);
                if (p)
                {
                    pointers[live_count] = p;
                    sizes[live_count] = sz;
                    live_count++;
                    args->allocations++;
                }
                else
                {
                    args->failed_allocations++;
                }
            }
            else if (live_count > 0)
            {
                size_t idx = rng() % live_count;
                args->resource->deallocate(pointers[idx], sizes[idx]);
                pointers[idx] = pointers[live_count - 1];
                sizes[idx] = sizes[live_count - 1];
                live_count--;
                args->deallocations++;
            }

            if ((args->allocations + args->deallocations) % 1000 == 0)
            {
                sched_yield();
            }
        }

        for (size_t i = 0; i < live_count; ++i)
        {
            args->resource->deallocate(pointers[i], sizes[i]);
            args->deallocations++;
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
        
        size_t SOAK_DURATION_SEC = 2;
        const char* soak_duration_env = getenv("ALLOCATOR_SOAK_DURATION");
        if (soak_duration_env)
        {
            SOAK_DURATION_SEC = atoi(soak_duration_env);
        }
        
        printf("\nRunning Allocator SoakTest for %zu seconds...\n", SOAK_DURATION_SEC);

        lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);
        
        struct sigaction sa = {};
        sa.sa_handler = sigusr1_nested_alloc_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGUSR1, &sa, nullptr);

        s_intr_resource = &resource;
        s_intr_signal_count = 0;
        s_intr_nested_count = 0;

        minstd::atomic<bool> stop_flag{false};
        
        pthread_t workers[NUM_THREADS]{};
        soak_thread_args thread_args[NUM_THREADS]{};
        
        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            thread_args[i].resource = &resource;
            thread_args[i].stop_flag = &stop_flag;
            thread_args[i].rng_seed = 987654321ULL + i;
            thread_args[i].id = i;
            
            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, soak_worker_thread, &thread_args[i]));
        }
        
        soak_bomber_args b_args;
        b_args.targets = workers;
        b_args.num_targets = NUM_THREADS;
        b_args.stop_flag = &stop_flag;
        
        pthread_t bomber;
        CHECK_EQUAL(0, pthread_create(&bomber, nullptr, soak_bomber_thread, &b_args));

        size_t elapsed = 0;
        size_t last_allocs = 0;
        size_t last_deallocs = 0;
        size_t last_failed = 0;
        while (elapsed < SOAK_DURATION_SEC * 10)
        {
            if (elapsed % 100 == 0 && elapsed > 0)
            {
                size_t c_allocs = 0, c_deallocs = 0, c_failed = 0;
                for (size_t i = 0; i < NUM_THREADS; ++i)
                {
                    c_allocs += thread_args[i].allocations;
                    c_deallocs += thread_args[i].deallocations;
                    c_failed += thread_args[i].failed_allocations;
                }

                size_t allocs_per_sec = (c_allocs - last_allocs) / 10;
                size_t deallocs_per_sec = (c_deallocs - last_deallocs) / 10;
                size_t failed_per_sec = (c_failed - last_failed) / 10;
                last_allocs = c_allocs;
                last_deallocs = c_deallocs;
                last_failed = c_failed;

                printf("Elapsed: %zu secs, Allocs: %zu ( %zu /sec ), Deallocs: %zu ( %zu /sec ), Failed: %zu ( %zu /sec )\n",
                       elapsed / 10, c_allocs, allocs_per_sec, c_deallocs, deallocs_per_sec, c_failed, failed_per_sec);
                fflush(stdout);
            }
            usleep(100000); // 100ms
            elapsed++;
        }

        stop_flag.store(true, minstd::memory_order_release);

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            pthread_join(workers[i], nullptr);
        }
        
        pthread_join(bomber, nullptr);
        
        sa.sa_handler = SIG_DFL;
        sigaction(SIGUSR1, &sa, nullptr);
        s_intr_resource = nullptr;

        size_t total_alloc = 0;
        size_t total_dealloc = 0;
        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            total_alloc += thread_args[i].allocations;
            total_dealloc += thread_args[i].deallocations;
        }

        printf("Soak test completed. Worker Allocs: %zu, Worker Deallocs: %zu, Failed Allocs: %zu\n",
               total_alloc, total_dealloc, thread_args[0].failed_allocations);
        printf("Signals delivered: %d, Nested allocs triggered: %d\n", (int)s_intr_signal_count, (int)s_intr_nested_count);
        
        // Assert no leaks 
        CHECK_EQUAL(0, resource.current_bytes_allocated());
        CHECK_EQUAL(total_alloc + s_intr_nested_count, resource.total_allocations());
        CHECK_EQUAL(total_dealloc + s_intr_nested_count, resource.total_deallocations());
    }
}
