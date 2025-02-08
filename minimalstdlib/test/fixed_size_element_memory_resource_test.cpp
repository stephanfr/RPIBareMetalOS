// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/malloc_free_wrapper_memory_resource.h>
#include <memory_resource>

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>

#include <sched.h>
#include <stdio.h>
#include <unistd.h>

extern "C" int get_current_core()
{
    return sched_getcpu();
}

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FixedSizeElementMemoryResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t NUM_ELEMENTS_IN_MULTITHREAD_TEST = 16000;

    constexpr size_t default_alignment = alignof(max_align_t);

    constexpr size_t buffer_size = 128 * 1048576; // 128 MB
    char buffer[buffer_size];

    minstd::atomic<bool> start_allocations = false;

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, NUM_ELEMENTS_IN_MULTITHREAD_TEST> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ELEMENTS_IN_MULTITHREAD_TEST> sizes_allocated = {0};
        minstd::array<bool, NUM_ELEMENTS_IN_MULTITHREAD_TEST> deleted_element = {false};
        float duration;
    };

    void *allocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        minstd::array<size_t, NUM_ELEMENTS_IN_MULTITHREAD_TEST> sizes;
        minstd::array<bool, NUM_ELEMENTS_IN_MULTITHREAD_TEST> deallocate_operation = {false};
        minstd::array<size_t, NUM_ELEMENTS_IN_MULTITHREAD_TEST> deallocation_index = {0};

        for (size_t i = 0; i < NUM_ELEMENTS_IN_MULTITHREAD_TEST; i++)
        {
            sizes[i] = rng() % 256;
            deallocate_operation[i] = false;
        }

        for (size_t i = 100; i < NUM_ELEMENTS_IN_MULTITHREAD_TEST; i++)
        {
            deallocate_operation[i] = ((rng() % 5) == 0);

            if (deallocate_operation[i])
            {
                deallocation_index[i] = rng() % i;
            }
        }

        while (!start_allocations)
        {
            sched_yield();
        }

        auto start = clock();

        for (size_t i = 0; i < NUM_ELEMENTS_IN_MULTITHREAD_TEST; i++)
        {
            void *ptr = args->mem_resource->allocate(sizes[i]);

            CHECK(ptr != nullptr);

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

        auto end = clock();

        args->duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        return nullptr;
    }

    TEST(FixedSizeElementMemoryResourceTests, SingleBlockResourceBasicFunctionality)
    {
        minstd::pmr::single_block_resource upstream_resource(buffer, buffer_size);

        minstd::pmr::fixed_size_element_resource<32, 2024, 1> resource(static_cast<minstd::pmr::memory_resource *>(&upstream_resource));

        void *ptr1 = resource.allocate(50);

        CHECK(ptr1 != nullptr);
        CHECK((unsigned long)ptr1 % default_alignment == 0);

        CHECK(resource.current_allocated() == 1);
        CHECK(resource.current_bytes_allocated() == 64);
        CHECK(resource.peak_allocated() == 1);
        CHECK(resource.total_deallocations() == 0);

        void *ptr2 = resource.allocate(50);

        CHECK(ptr2 != nullptr);
        CHECK((unsigned long)ptr2 % default_alignment == 0);
        CHECK(ptr2 > ptr1);
        CHECK((((unsigned long)ptr2 - (unsigned long)ptr1)) % default_alignment == 0);
        CHECK_EQUAL(64, ((unsigned long)ptr2 - (unsigned long)ptr1)); //  The difference between the two pointers should be 64 bytes

        CHECK(resource.current_allocated() == 2);
        CHECK(resource.current_bytes_allocated() == 64 + 64);
        CHECK(resource.peak_allocated() == 2);
        CHECK(resource.total_deallocations() == 0);

        void *ptr3 = resource.allocate(133);

        CHECK(ptr3 != nullptr);
        CHECK((unsigned long)ptr3 % default_alignment == 0);
        CHECK(ptr3 > ptr2);
        CHECK((((unsigned long)ptr3 - (unsigned long)ptr2)) % default_alignment == 0);
        CHECK_EQUAL(64, ((unsigned long)ptr3 - (unsigned long)ptr2)); //  The difference between the two pointers should be 64 bytes

        CHECK(resource.current_allocated() == 3);
        CHECK(resource.current_bytes_allocated() == 64 + 64 + 160);
        CHECK(resource.peak_allocated() == 3);
        CHECK(resource.total_deallocations() == 0);

        void *ptr4 = resource.allocate(32);

        CHECK(ptr4 != nullptr);
        CHECK((unsigned long)ptr4 % default_alignment == 0);
        CHECK(ptr4 > ptr3);
        CHECK((((unsigned long)ptr4 - (unsigned long)ptr3)) % default_alignment == 0);
        CHECK_EQUAL(160, ((unsigned long)ptr4 - (unsigned long)ptr3)); //  The difference between the two pointers should be 160 bytes, the size of ptr3 block

        CHECK(resource.current_allocated() == 4);
        CHECK(resource.current_bytes_allocated() == 64 + 64 + 160 + 32);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 0);

        //  Deallocate ptr2

        resource.deallocate(ptr2, 50);

        CHECK(resource.current_allocated() == 3);
        CHECK(resource.current_bytes_allocated() == 64 + 160 + 32);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 1);

        //  Deallocate ptr4

        resource.deallocate(ptr4, 32);

        CHECK(resource.current_allocated() == 2);
        CHECK(resource.current_bytes_allocated() == 64 + 160);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 2);
    }

    TEST(FixedSizeElementMemoryResourceTests, GrowthUntilMemoryExhaustionTest)
    {
        minstd::pmr::single_block_resource upstream_resource(buffer, buffer_size);

        minstd::pmr::fixed_size_element_resource<32, 2048, 5> resource(static_cast<minstd::pmr::memory_resource *>(&upstream_resource));

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(123, 456));

        size_t total_bytes_allocated = 0;
        size_t total_allocations = 0;

        auto start = clock();

        do
        {
            auto size = rng() % 512;

            auto next_allocation = resource.allocate(size);

            if (next_allocation == nullptr)
            {
                break;
            }

            total_allocations++;
            total_bytes_allocated += size;
        } while (true);

        auto end = clock();

        auto duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Duration fixed size element allocator exhaust heap: %f\n", duration);
        printf("Total Allocations: %ld\n", total_allocations);

        CHECK_EQUAL(resource.current_allocated(), total_allocations);

        minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(static_cast<minstd::pmr::memory_resource *>(&upstream_resource));

        minstd::Xoroshiro128PlusPlusRNG rng1(minstd::Xoroshiro128PlusPlusRNG::Seed(123, 456));

        start = clock();

        for (size_t i = 0; i < total_allocations; i++)
        {
            auto size = rng1() % 512;

            auto next_allocation = malloc_free_resource.allocate(size);

            CHECK(next_allocation != nullptr);
        }
        end = clock();

        duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Duration malloc/free exhaust heap: %f\n", duration);
    }

    TEST(FixedSizeElementMemoryResourceTests, MultiThreadTest)
    {
        constexpr size_t NUM_THREADS = 8;
        constexpr size_t NUM_BYTES_PER_ELEMENT = 32;
        constexpr size_t NUM_ELEMENTS_PER_BLOCK = 1024;

        minstd::pmr::single_block_resource upstream_resource(buffer, buffer_size);

        auto *resource = new minstd::pmr::fixed_size_element_resource<NUM_BYTES_PER_ELEMENT, NUM_ELEMENTS_PER_BLOCK, 8, false>(static_cast<minstd::pmr::memory_resource *>(&upstream_resource));

        minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(static_cast<minstd::pmr::memory_resource *>(&upstream_resource));

        allocator_thread_arguments args[NUM_THREADS];
        pthread_t threads[NUM_THREADS];

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = resource;
            args[i].rng_seed = clock() + i * 1000;

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

        auto summed_duration = 0.0;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            summed_duration += args[i].duration;
        }

        printf("Duration fixed_size_element_resource multi-thread test: %f\n", summed_duration);

        size_t total_number_of_allocations = 0;
        size_t total_number_of_bytes_allocated = 0;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            for (size_t j = 0; j < NUM_ELEMENTS_IN_MULTITHREAD_TEST; j++)
            {
                if (args[i].pointers_allocated[j] == nullptr)
                {
                    break;
                }

                //  Make sure total allocations and sizes match.  The resource tracks total bytes allocated - even if the requested
                //      size was smaller - so we need to adjust for that.

                if (!args[i].deleted_element[j])
                {
                    total_number_of_allocations++;
                    total_number_of_bytes_allocated += args[i].sizes_allocated[j] % NUM_BYTES_PER_ELEMENT == 0 ? args[i].sizes_allocated[j] : ((args[i].sizes_allocated[j] / NUM_BYTES_PER_ELEMENT) + 1) * NUM_BYTES_PER_ELEMENT;
                }
            }
        }

        printf("Number of Blocks: %ld\n", resource->number_of_blocks());

        printf("Percent Filled: %f\n", (double)total_number_of_bytes_allocated / (double)(resource->number_of_blocks() * NUM_BYTES_PER_ELEMENT * NUM_ELEMENTS_PER_BLOCK));

        //  Malloc-Free for baseline comparison

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &malloc_free_resource;

            CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
        }

        sleep(1);

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        summed_duration = 0.0;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            summed_duration += args[i].duration;
        }

        printf("Duration malloc-free multi-thread test: %f\n", summed_duration);

        delete resource;

        //        CHECK_EQUAL(total_number_of_allocations, resource.current_allocated());
        //        CHECK_EQUAL(total_number_of_bytes_allocated, resource.current_bytes_allocated());
    }
    /*
            //  The benchmarks are interesting but that is about all.  The lockfree behavior of aarch64 is so different
            //      from x64 that generalization here doesn't make much sense.  I have them just to insure that there
            //      is not a huge difference between the two implementations on the x64 platform at least.

            TEST(FixedSizeElementMemoryResourceTests, Benchmark)
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

        //        printf("Duration SBMR: %f\n", duration_smbr);

                start = clock();

                for (size_t i = 0; i < NUM_ALLOCATIONS; i++)
                {
                    pointers[i] = malloc(sizes[i]);
                }

                end = clock();

                auto duration_malloc = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        //        printf("Duration malloc: %f\n", duration_malloc);

                CHECK(duration_smbr < (duration_malloc * 1.5));     //  Worst case, the SMBR test timing should be no more than 1.5 times longer than malloc
            }
        */
}
