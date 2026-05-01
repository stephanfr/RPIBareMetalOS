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

extern "C" double sqrt(double);
extern "C" double log(double);
extern "C" double cos(double);
extern "C" double exp(double);

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (LockfreeBitblockMemoryResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t NUM_ELEMENTS_IN_MULTITHREAD_TEST = 16000;
    constexpr size_t MAX_NUMBER_OF_BLOCKS = 12;
    constexpr size_t MAX_NUMBER_OF_ARENAS = 32;

    constexpr size_t DEFAULT_ALIGNMENT = alignof(max_align_t);

    constexpr size_t BUFFER_SIZE = 128 * 1048576; // 128 MB
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
        minstd::array<void *, NUM_ELEMENTS_IN_MULTITHREAD_TEST> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ELEMENTS_IN_MULTITHREAD_TEST> sizes_allocated = {0};
        minstd::array<bool, NUM_ELEMENTS_IN_MULTITHREAD_TEST> deleted_element = {false};
        float duration;
    };

    constexpr size_t NUM_PERF_ALLOCATIONS_PER_THREAD = 2000;
    constexpr size_t MAX_ALLOCATION_SIZE = 1000;

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

    struct perf_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, NUM_PERF_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_PERF_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        size_t repetitions = 0;
    };

    void *allocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        minstd::array<size_t, NUM_ELEMENTS_IN_MULTITHREAD_TEST> sizes;

        for (size_t i = 0; i < NUM_ELEMENTS_IN_MULTITHREAD_TEST; i++)
        {
            sizes[i] = rng() % 256;
        }

        while (!start_allocations)
        {
            sched_yield();
        }

        auto start = clock();

        for (size_t i = 0; i < NUM_ELEMENTS_IN_MULTITHREAD_TEST; i++)
        {
            void *ptr = args->mem_resource->allocate(sizes[i]);

            if (ptr == nullptr)
            {
                break;
            }

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = sizes[i];
        }

        auto end = clock();

        args->duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        return nullptr;
    }

    void *deallocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        while (!start_allocations)
        {
            sched_yield();
        }

        for (size_t i = 0; i < NUM_ELEMENTS_IN_MULTITHREAD_TEST; i++)
        {
            if (args->pointers_allocated[i] != nullptr && !args->deleted_element[i])
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
                args->deleted_element[i] = true;
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

        for (size_t i = 0; i < NUM_PERF_ALLOCATIONS_PER_THREAD; i++)
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
            for (size_t i = 0; i < NUM_PERF_ALLOCATIONS_PER_THREAD; i++)
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

        for (size_t i = 0; i < NUM_PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    TEST(LockfreeBitblockMemoryResourceTests, SingleBlockResourceBasicFunctionality)
    {
        minstd::pmr::single_block_resource upstream_resource(buffer, BUFFER_SIZE);

        minstd::pmr::lockfree_bitblock_resource<32, 2024, MAX_NUMBER_OF_ARENAS, MAX_NUMBER_OF_BLOCKS> resource(
            static_cast<minstd::pmr::memory_resource *>(&upstream_resource),
            get_number_of_arenas());

        void *ptr1 = resource.allocate(50);

        CHECK(ptr1 != nullptr);
        CHECK((unsigned long)ptr1 % DEFAULT_ALIGNMENT == 0);

        CHECK(resource.current_allocated() == 1);
        CHECK(resource.current_bytes_allocated() == 96);
        CHECK(resource.peak_allocated() == 1);
        CHECK(resource.total_deallocations() == 0);

        void *ptr2 = resource.allocate(50);

        CHECK(ptr2 != nullptr);
        CHECK((unsigned long)ptr2 % DEFAULT_ALIGNMENT == 0);
        CHECK(ptr2 > ptr1);
        CHECK((((unsigned long)ptr2 - (unsigned long)ptr1)) % DEFAULT_ALIGNMENT == 0);
        CHECK_EQUAL(96, ((unsigned long)ptr2 - (unsigned long)ptr1)); //  The difference between the two pointers should be 96 bytes

        CHECK(resource.current_allocated() == 2);
        CHECK(resource.current_bytes_allocated() == 96 + 96);
        CHECK(resource.peak_allocated() == 2);
        CHECK(resource.total_deallocations() == 0);

        void *ptr3 = resource.allocate(133);

        CHECK(ptr3 != nullptr);
        CHECK((unsigned long)ptr3 % DEFAULT_ALIGNMENT == 0);
        CHECK(ptr3 > ptr2);
        CHECK((((unsigned long)ptr3 - (unsigned long)ptr2)) % DEFAULT_ALIGNMENT == 0);
        CHECK_EQUAL(96, ((unsigned long)ptr3 - (unsigned long)ptr2)); //  The difference between the two pointers should be 96 bytes

        CHECK(resource.current_allocated() == 3);
        CHECK(resource.current_bytes_allocated() == 96 + 96 + 160);
        CHECK(resource.peak_allocated() == 3);
        CHECK(resource.total_deallocations() == 0);

        void *ptr4 = resource.allocate(32);

        CHECK(ptr4 != nullptr);
        CHECK((unsigned long)ptr4 % DEFAULT_ALIGNMENT == 0);
        CHECK(ptr4 > ptr3);
        CHECK((((unsigned long)ptr4 - (unsigned long)ptr3)) % DEFAULT_ALIGNMENT == 0);
        CHECK_EQUAL(160, ((unsigned long)ptr4 - (unsigned long)ptr3)); //  The difference between the two pointers should be 160 bytes, the size of ptr3 block

        CHECK(resource.current_allocated() == 4);
        CHECK(resource.current_bytes_allocated() == 96 + 96 + 160 + 64);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 0);

        //  Deallocate ptr2

        resource.deallocate(ptr2, 50);

        CHECK(resource.current_allocated() == 3);
        CHECK(resource.current_bytes_allocated() == 96 + 160 + 64);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 1);

        //  Deallocate ptr4

        resource.deallocate(ptr4, 32);

        CHECK(resource.current_allocated() == 2);
        CHECK(resource.current_bytes_allocated() == 96 + 160);
        CHECK(resource.peak_allocated() == 4);
        CHECK(resource.total_deallocations() == 2);
    }

    TEST(LockfreeBitblockMemoryResourceTests, GrowthUntilMemoryExhaustionTest)
    {
        minstd::pmr::single_block_resource upstream_resource(buffer, BUFFER_SIZE);

        minstd::pmr::lockfree_bitblock_resource<32, 2048, MAX_NUMBER_OF_ARENAS, MAX_NUMBER_OF_BLOCKS> resource(
            static_cast<minstd::pmr::memory_resource *>(&upstream_resource),
            get_number_of_arenas());

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

        printf("Duration lockfree bitblock allocator exhaust heap: %f\n", duration);
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

    TEST(LockfreeBitblockMemoryResourceTests, MultiThreadTest)
    {
        constexpr size_t NUM_THREADS = 8;
        constexpr size_t NUM_BYTES_PER_ELEMENT = 32;
        constexpr size_t NUM_ELEMENTS_PER_BLOCK = 1024;

        minstd::pmr::single_block_resource upstream_resource(buffer, BUFFER_SIZE);

        auto *resource = new minstd::pmr::lockfree_bitblock_resource<NUM_BYTES_PER_ELEMENT, NUM_ELEMENTS_PER_BLOCK, MAX_NUMBER_OF_ARENAS, MAX_NUMBER_OF_BLOCKS, false>(
            static_cast<minstd::pmr::memory_resource *>(&upstream_resource),
            get_number_of_arenas());

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

        printf("Duration lockfree_bitblock_resource multi-thread test: %f\n", summed_duration);

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
        CHECK(resource->number_of_blocks() <= MAX_NUMBER_OF_BLOCKS);

        printf("Percent Filled: %f\n", (double)total_number_of_bytes_allocated / (double)(resource->number_of_blocks() * NUM_BYTES_PER_ELEMENT * NUM_ELEMENTS_PER_BLOCK));

        //  Deallocate the allocations

        start_allocations = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_create(&threads[i], NULL, deallocation_thread, (void *)&args[i]) == 0);
        }

        sleep(1);

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        //  Malloc-Free for baseline comparison

        start_allocations = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            args[i].mem_resource = &malloc_free_resource;
            args[i].pointers_allocated.fill(nullptr);
            args[i].sizes_allocated.fill(0);
            args[i].deleted_element.fill(false);

            CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
        }

        sleep(1);

        start_allocations = true;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_join(threads[i], NULL) == 0);
        }

        start_allocations = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK(pthread_create(&threads[i], NULL, deallocation_thread, (void *)&args[i]) == 0);
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

    TEST(LockfreeBitblockMemoryResourceTests, ThreadSensitivityPerformanceTest)
    {
        constexpr size_t NUM_BYTES_PER_ELEMENT = 64;
        constexpr size_t NUM_ELEMENTS_PER_BLOCK = 1024;
        constexpr size_t MAX_THREADS = 16;
        constexpr size_t REPETITIONS = 500;
        constexpr size_t PERF_MAX_BLOCKS = 512;

        printf("\n=== Lockfree Bitblock Resource Thread Scalability ===\n");
        printf("Threads | Bitblock Ops/sec | Malloc Ops/sec | BB Efficiency | Malloc Eff | BB Scale\n");
        printf("--------|------------------|----------------|---------------|------------|----------\n");

        static double baseline_bitblock_efficiency = 0.0;

        for (size_t num_threads = 1; num_threads <= MAX_THREADS; ++num_threads)
        {
            start_allocations = false;

            minstd::pmr::single_block_resource upstream_resource(buffer, BUFFER_SIZE);

            auto *resource = new minstd::pmr::lockfree_bitblock_resource<NUM_BYTES_PER_ELEMENT, NUM_ELEMENTS_PER_BLOCK, MAX_NUMBER_OF_ARENAS, PERF_MAX_BLOCKS, false>(
                static_cast<minstd::pmr::memory_resource *>(&upstream_resource),
                get_number_of_arenas());

            perf_thread_arguments args[MAX_THREADS];
            pthread_t threads[MAX_THREADS];

            size_t total_allocation_operations = num_threads * NUM_PERF_ALLOCATIONS_PER_THREAD;
            size_t total_deallocation_operations = num_threads * NUM_PERF_ALLOCATIONS_PER_THREAD * REPETITIONS;
            size_t total_operations = total_allocation_operations + total_deallocation_operations;

            //  Lockfree bitblock resource test

            for (size_t i = 0; i < num_threads; i++)
            {
                args[i].mem_resource = resource;
                args[i].rng_seed = i + 1000 + num_threads * 100;
                args[i].repetitions = REPETITIONS;

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

            double bitblock_elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                            (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

            double bitblock_total_ops_per_second = total_operations / bitblock_elapsed_time;
            double bitblock_efficiency = bitblock_total_ops_per_second / num_threads;

            delete resource;

            //  Malloc/Free baseline

            start_allocations = false;

            minstd::pmr::malloc_free_wrapper_memory_resource malloc_resource(nullptr);

            for (size_t i = 0; i < num_threads; i++)
            {
                args[i].mem_resource = &malloc_resource;
                args[i].rng_seed = i + 2000 + num_threads * 100;
                args[i].repetitions = REPETITIONS;

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
                baseline_bitblock_efficiency = bitblock_efficiency;
            }

            double bitblock_scalability = baseline_bitblock_efficiency > 0
                ? (bitblock_efficiency / baseline_bitblock_efficiency)
                : 1.0;

            printf("  %2zu   | %15.0f | %12.0f | %11.0f | %8.0f | %8.2fx\n",
                   num_threads,
                   bitblock_total_ops_per_second,
                   malloc_total_ops_per_second,
                   bitblock_efficiency,
                   malloc_efficiency,
                   bitblock_scalability);

            CHECK(bitblock_elapsed_time > 0);
            CHECK(malloc_elapsed_time > 0);
            CHECK(bitblock_total_ops_per_second > 0);
            CHECK(malloc_total_ops_per_second > 0);

            usleep(100000); // 100ms
        }

        printf("\nNotes:\n");
        printf("- Ops/sec: Total allocation + deallocation operations per second\n");
        printf("- Efficiency: Total operations per second per thread\n");
        printf("- BB Scale: Bitblock efficiency relative to single-thread baseline\n");
        printf("- Each thread performs %zu initial allocations + %zu repetitions\n",
               NUM_PERF_ALLOCATIONS_PER_THREAD, REPETITIONS);
        printf("- Allocation sizes: lognormal(5.4, 1.2) distribution, clamped to [1, %zu] bytes, element size: %zu bytes\n",
               MAX_ALLOCATION_SIZE, NUM_BYTES_PER_ELEMENT);
    }
    /*
            //  The benchmarks are interesting but that is about all.  The lockfree behavior of aarch64 is so different
            //      from x64 that generalization here doesn't make much sense.  I have them just to insure that there
            //      is not a huge difference between the two implementations on the x64 platform at least.

            TEST(LockfreeBitblockMemoryResourceTests, Benchmark)
            {
                minstd::pmr::single_block_resource resource(buffer, BUFFER_SIZE);

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
