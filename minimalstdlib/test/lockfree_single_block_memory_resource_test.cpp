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

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (LockfreeSingleBlockMemoryResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t default_alignment = alignof(max_align_t);

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 5000;

    constexpr size_t buffer_size = (1024 + 256) * 1048576; // 1 GB 256 MB
    char buffer[buffer_size];

    minstd::atomic<bool> start_allocations = false;
    minstd::atomic<bool> exit_thread = false;

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, NUM_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deleted_element = {false};
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
                args->mem_resource->deallocate( args->pointers_allocated[i], args->sizes_allocated[i] );

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

            for (auto itr = ((minstd::pmr::lockfree_single_block_resource *)(args->mem_resource))->begin(); itr != ((minstd::pmr::lockfree_single_block_resource *)(args->mem_resource))->end(); ++itr)
            {
                auto alloc_info = *itr;

                if (alloc_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE)
                {
                    in_use++;
                }
                else if (alloc_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::AVAILABLE)
                {
                    available++;
                }
                else if (alloc_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::SOFT_DELETED)
                {
                    soft_deleted++;
                }
                else if (alloc_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::LOCKED)
                {
                    locked++;
                }
                else if (alloc_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::METADATA_AVAILABLE)
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
        minstd::pmr::lockfree_single_block_resource resource(buffer, buffer_size);

        void *ptr1 = resource.allocate(50);

        CHECK(ptr1 != nullptr);
        CHECK((unsigned long)ptr1 % default_alignment == 0);

        auto allocation_info = resource.get_allocation_info(ptr1);
        CHECK(allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE);
        CHECK(allocation_info.size == 50);
        CHECK(allocation_info.alignment == default_alignment);

        void *ptr2 = resource.allocate(1023);

        CHECK(ptr2 != nullptr);
        CHECK((unsigned long)ptr2 % default_alignment == 0);

        allocation_info = resource.get_allocation_info(ptr2);
        CHECK(allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE);
        CHECK(allocation_info.size == 1023);
        CHECK(allocation_info.alignment == default_alignment);

        void *ptr3 = resource.allocate(123);

        CHECK(ptr3 != nullptr);
        CHECK((unsigned long)ptr3 % default_alignment == 0);

        allocation_info = resource.get_allocation_info(ptr3);
        CHECK(allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE);
        CHECK(allocation_info.size == 123);
        CHECK(allocation_info.alignment == default_alignment);

        void *ptr4 = resource.allocate(45678);

        CHECK(ptr4 != nullptr);
        CHECK((unsigned long)ptr4 % default_alignment == 0);

        allocation_info = resource.get_allocation_info(ptr4);
        CHECK(allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE);
        CHECK(allocation_info.size == 45678);
        CHECK(allocation_info.alignment == default_alignment);

        int counter = 0;

        for (auto iter = resource.begin(); iter != resource.end(); ++iter)
        {
            allocation_info = *iter;

            CHECK(allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE);

            counter++;
        }

        CHECK(counter == 4);

        resource.deallocate(ptr4, 45678);

        counter = 0;

        for (auto iter = resource.begin(); iter != resource.end(); ++iter)
        {
            allocation_info = *iter;

            if (allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE)
            {
                counter++;
            }
        }

        CHECK(counter == 3);

        void *ptr5 = resource.allocate(100);

        CHECK(ptr5 != nullptr);
        CHECK((unsigned long)ptr5 % default_alignment == 0);

        allocation_info = resource.get_allocation_info(ptr5);
        CHECK(allocation_info.state == minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE);
        CHECK(allocation_info.size == 100);
        CHECK(allocation_info.alignment == default_alignment);
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, MultiThreadTest)
    {
        constexpr size_t NUM_THREADS = 16;

        start_allocations = false;
        exit_thread = false;

        minstd::pmr::lockfree_single_block_resource resource(buffer, buffer_size);

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

        auto start = clock();

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        auto end = clock();

        exit_thread = true;

        for (size_t i = 0; i < 5; i++)
        {
            CHECK(pthread_join(itr_threads[i], NULL) == 0);
        }

        auto duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

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

                    if (!alloc_info.state != minstd::pmr::lockfree_single_block_resource::allocation_state::INVALID)
                    {
                        printf("Invalid allocation info\n");
                    }

                    CHECK(alloc_info.state != minstd::pmr::lockfree_single_block_resource::allocation_state::INVALID);

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
            if ((*itr).state != minstd::pmr::lockfree_single_block_resource::allocation_state::IN_USE)
            {
                if ((*itr).state == minstd::pmr::lockfree_single_block_resource::allocation_state::AVAILABLE)
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

        start = clock();

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        end = clock();

        duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Malloc Free Resource Multithread Tests Duration: %f\n", duration);
    }

    TEST(LockfreeSingleBlockMemoryResourceTests, MultiThreadAllocateDeallocateTest)
    {
        constexpr size_t NUM_THREADS = 2;

        start_allocations = false;

        minstd::pmr::lockfree_single_block_resource resource(buffer, buffer_size);

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

        auto start = clock();

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        auto end = clock();
        auto duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;
        printf("Lockfree Single Block Resource Multithread Tests Duration: %f\n", duration);

        //  Force two reclaimation passes, with no other threads running this should move all
        //      metadata records into the free headers list.  The free block bins should be empty.

//        resource.reclaim();
//        resource.reclaim();
//        resource.reclaim();
//        resource.reclaim();
//        resource.reclaim();
//        resource.reclaim();


        printf("Total number of allocations: %zu  deallocations: %zu\n", resource.total_allocations(), resource.total_deallocations());
        printf("Current allocations: %zu  bytes allocated: %zu\n", resource.current_allocated(), resource.current_bytes_allocated());

        CHECK_EQUAL(0, resource.current_allocated());
        CHECK_EQUAL(0, resource.current_bytes_allocated());

        //  Do it again with malloc/free

        start_allocations = false;

        minstd::pmr::malloc_free_wrapper_memory_resource  malloc_free_resource(nullptr);

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &malloc_free_resource;

            CHECK(pthread_create(&threads[i], NULL, repeated_allocation_deallocation_thread, (void *)&args[i]) == 0);
        }

        sleep(1);

        start = clock();

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        end = clock();
        duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;
        printf("Malloc Free Resource Multithread Tests Duration: %f\n", duration);
    }
}
