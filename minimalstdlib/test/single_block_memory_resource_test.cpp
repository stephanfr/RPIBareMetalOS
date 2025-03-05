// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
/*
#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <memory_resource>

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>

#include <unistd.h>
#include <stdio.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SingleBlockMemoryResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t default_alignment = alignof(max_align_t);

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 10000;

    constexpr size_t buffer_size = 64 * 1048576; // 64 MB
    char buffer[buffer_size];

    minstd::atomic<bool> start_allocations = false;

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, NUM_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deleted_element = {false};
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
            sizes[i] = 256 + (rng() % 8192);
        }

        for (size_t i = 50; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            deallocate_operation[i] = ((rng() % 4) == 0);

            if (deallocate_operation[i])
            {
                deallocation_index[i] = rng() % i;
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

    TEST(SingleBlockMemoryResourceTests, SingleBlockResourceBasicFunctionality)
    {
        minstd::pmr::single_block_resource resource(buffer, buffer_size);

        void *ptr1 = resource.allocate(50);

        CHECK(ptr1 != nullptr);
        CHECK((unsigned long)ptr1 % default_alignment == 0);

        auto alloc_info = resource.get_allocation_info(ptr1);

        CHECK(alloc_info.is_valid);
        CHECK(alloc_info.in_use);
        CHECK(alloc_info.size == 50);

        void *ptr2 = resource.allocate(50);

        CHECK(ptr2 != nullptr);
        CHECK((unsigned long)ptr2 % default_alignment == 0);
        CHECK(ptr2 > ptr1);
        CHECK((((unsigned long)ptr2 - (unsigned long)ptr1)) % default_alignment == 0);
        CHECK_EQUAL(112, ((unsigned long)ptr2 - (unsigned long)ptr1)); //  The difference between the two pointers should be 112 bytes (64 for the 50 byte block + a 48 byte header)

        alloc_info = resource.get_allocation_info(ptr2);

        CHECK(alloc_info.is_valid);
        CHECK(alloc_info.in_use);
        CHECK(alloc_info.size == 50);

        void *ptr3 = resource.allocate(133);

        CHECK(ptr3 != nullptr);
        CHECK((unsigned long)ptr3 % default_alignment == 0);
        CHECK(ptr3 > ptr2);
        CHECK((((unsigned long)ptr3 - (unsigned long)ptr2)) % default_alignment == 0);
        CHECK_EQUAL(112, ((unsigned long)ptr3 - (unsigned long)ptr2));

        alloc_info = resource.get_allocation_info(ptr3);

        CHECK(alloc_info.is_valid);
        CHECK(alloc_info.in_use);
        CHECK(alloc_info.size == 133);

        void *ptr4 = resource.allocate(64);

        CHECK(ptr4 != nullptr);
        CHECK((unsigned long)ptr4 % default_alignment == 0);
        CHECK(ptr4 > ptr3);
        CHECK((((unsigned long)ptr4 - (unsigned long)ptr3)) % default_alignment == 0);
        CHECK_EQUAL(192, ((unsigned long)ptr4 - (unsigned long)ptr3));

        alloc_info = resource.get_allocation_info(ptr4);

        CHECK(alloc_info.is_valid);
        CHECK(alloc_info.in_use);
        CHECK(alloc_info.size == 64);

        CHECK(resource.current_allocated() == 4);
        CHECK(resource.current_bytes_allocated() == 50 + 50 + 133 + 64);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 0);

        //  Deallocate the first delete ptr2

        resource.deallocate(ptr2, 50);

        alloc_info = resource.get_allocation_info(ptr2);

        CHECK(alloc_info.is_valid);
        CHECK(!alloc_info.in_use);
        CHECK(alloc_info.size == 50);

        CHECK(resource.current_allocated() == 3);
        CHECK(resource.current_bytes_allocated() == 50 + 133 + 64);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 1);

        //  Deallocate ptr4

        resource.deallocate(ptr4, 64);

        alloc_info = resource.get_allocation_info(ptr4);

        CHECK(alloc_info.is_valid);
        CHECK(!alloc_info.in_use);
        CHECK(alloc_info.size == 64);

        CHECK(resource.current_allocated() == 2);
        CHECK(resource.current_bytes_allocated() == 50 + 133);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 2);

        //  Check iterator

        auto iter = resource.begin();

        CHECK(iter != resource.end());
        CHECK(*iter == resource.get_allocation_info(ptr4));

        ++iter;
        CHECK(iter != resource.end());
        CHECK(*iter == resource.get_allocation_info(ptr3));

        ++iter;
        CHECK(iter != resource.end());
        CHECK(*iter == resource.get_allocation_info(ptr2));

        ++iter;
        CHECK(iter != resource.end());
        CHECK(*iter == resource.get_allocation_info(ptr1));

        ++iter;
        CHECK(iter == resource.end());
    }

    TEST(SingleBlockMemoryResourceTests, MultiThreadTest)
    {
        constexpr size_t NUM_THREADS = 32;

        minstd::pmr::single_block_resource resource(buffer, buffer_size);

        allocator_thread_arguments args[NUM_THREADS];
        pthread_t threads[NUM_THREADS];

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &resource;
            args[i].rng_seed = i + 333;

            CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
        }

        //  Delay a bit for all threads to initialize themselves, then we want to release them so
        //      we can maximally stress the resource.  The count of collisions should provide a
        //      reasonable measure of the contention.

        sleep(1);

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        printf("Retries: %zu\n", resource.cmp_exchange_retries());

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

                auto alloc_info = resource.get_allocation_info(args[i].pointers_allocated[j]);

                CHECK(alloc_info.is_valid);

                if (!args[i].deleted_element[j])
                {
                    total_number_of_allocations++;
                    total_number_of_bytes_allocated += args[i].sizes_allocated[j];
                }
            }
        }

        CHECK_EQUAL(total_number_of_allocations, resource.current_allocated());
        CHECK_EQUAL(total_number_of_bytes_allocated, resource.current_bytes_allocated());
    }

    //  The benchmarks are interesting but that is about all.  The lockfree behavior of aarch64 is so different
    //      from x64 that generalization here doesn't make much sense.  I have them just to insure that there
    //      is not a huge difference between the two implementations on the x64 platform at least.

    TEST(SingleBlockMemoryResourceTests, Benchmark)
    {
        minstd::pmr::single_block_resource resource(buffer, buffer_size);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(100, 1000));

        constexpr size_t NUM_ALLOCATIONS = 5000;

        minstd::array<size_t, NUM_ALLOCATIONS> sizes;
        minstd::array<void *, NUM_ALLOCATIONS> pointers;

        for (size_t i = 0; i < NUM_ALLOCATIONS; i++)
        {
            sizes[i] = rng() % 256;
        }

        auto start = clock();

        for (size_t i = 0; i < NUM_ALLOCATIONS; i++)
        {
            pointers[i] = resource.allocate(sizes[i]);
        }

        auto end = clock();

        auto duration_smbr = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Duration SBMR: %f\n", duration_smbr);

        start = clock();

        for (size_t i = 0; i < NUM_ALLOCATIONS; i++)
        {
            pointers[i] = malloc(sizes[i]);
        }

        end = clock();

        auto duration_malloc = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Duration malloc: %f\n", duration_malloc);

//        CHECK(duration_smbr < (duration_malloc * 2));     //  Worst case, the SMBR test timing should be no more than 2 times longer than malloc
    }

}
*/
