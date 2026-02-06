// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/binary_semaphore_array2>

#include <array>
#include <pthread.h>
#include <random>

#include <stdio.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (BinarySemaphoreArrayTests)
    {
    };
#pragma GCC diagnostic pop

    minstd::atomic<bool> start_updates = false;

    struct bit_block
    {
        uint64_t start_;
        size_t length_;
    };

    size_t generate_non_overlapping_bit_blocks(size_t max_index,
                                               uint64_t rng_seed,
                                               minstd::array<bit_block, 1000> &bit_blocks)
    {
        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(rng_seed, rng_seed * 10));

        uint64_t last_start = 0;
        size_t last_size = 0;

        size_t i = 0;

        do
        {
            last_start += 1 + last_size + (rng() % 16);
            last_size = 1 + (rng() % 15);

            if (last_start + last_size >= max_index)
            {
                break;
            }

            bit_blocks[i++] = {last_start, last_size};

            if (i >= bit_blocks.size())
            {
                break;
            }
        } while (true);

        return i;
    }

    struct _thread_arguments
    {
        minstd::binary_semaphore_array2<16384> *semaphores_ = nullptr;
        uint64_t rng_seed_;

        uint32_t bits_acquired_ = 0;
    };

    enum class perf_op
    {
        single_bit,
        block,
        next_block
    };

    struct _perf_thread_arguments
    {
        minstd::binary_semaphore_array2<65536> *semaphores_ = nullptr;
        uint64_t rng_seed_ = 0;
        size_t iterations_ = 0;
        perf_op op_ = perf_op::single_bit;
    };

    void *_perf_worker(void *arguments)
    {
        _perf_thread_arguments *args = static_cast<_perf_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed_, args->rng_seed_ * 10));

        while (!start_updates)
        {
        }

        switch (args->op_)
        {
            case perf_op::single_bit:
                for (size_t i = 0; i < args->iterations_; i++)
                {
                    const size_t index = rng() % 65536;
                    args->semaphores_->acquire(index);
                    args->semaphores_->release(index);
                }
                break;
            case perf_op::block:
                for (size_t i = 0; i < args->iterations_; i++)
                {
                    const uint32_t block_size = (rng() % 16) + 1;
                    const size_t index = rng() % (65536 - block_size);
                    args->semaphores_->acquire_block(index, block_size);
                    args->semaphores_->release_block(index, block_size);
                }
                break;
            case perf_op::next_block:
                for (size_t i = 0; i < args->iterations_; i++)
                {
                    const uint32_t block_size = (rng() % 16) + 1;
                    auto index = args->semaphores_->acquire_next_empty_block(block_size, 0);

                    if (index.has_value())
                    {
                        args->semaphores_->release_block(index.value(), block_size);
                    }
                }
                break;
        }

        return nullptr;
    }

    void *_worker_thread1(void *arguments)
    {
        _thread_arguments *args = static_cast<_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed_, args->rng_seed_ * 10));

        minstd::array<bit_block, 1000> bit_blocks;

        size_t num_blocks = generate_non_overlapping_bit_blocks(16384, 12345, bit_blocks);

        //  Wait for the start signal, this will help insure the threads are all running at the same time
        //      and maximimally stress the lockfree behavior of the semaphore array.

        while (!start_updates)
        {
        }

        //  Try to acquire and rlease the blocks.  I have not figured out how to test the state of the
        //      semaphore array in this multi-threaded test, so we will simply trust that no exceptions
        //      means that the operations worked correctly.

        for (size_t i = 0; i < num_blocks; i++)
        {
            args->semaphores_->acquire_block(bit_blocks[i].start_, bit_blocks[i].length_);

            if (i > 25)
            {
                args->semaphores_->release_block(bit_blocks[i - 25].start_, bit_blocks[i - 25].length_);
            }
        }

        return nullptr;
    }

    void *_worker_thread2(void *arguments)
    {
        //  This worker thread competes to get next blocks, until there are no more blocks to get.

        _thread_arguments *args = static_cast<_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed_, args->rng_seed_ * 10));

        uint32_t block_size = (rng() % 16) + 1;

        auto block = args->semaphores_->acquire_next_empty_block(block_size);

        while (block.has_value())
        {
            args->bits_acquired_ += block_size;

            block_size = (rng() % 16) + 1;

            block = args->semaphores_->acquire_next_empty_block(block_size);
        }

        return nullptr;
    }

    TEST(BinarySemaphoreArrayTests, BasicFunctionality)
    {
        minstd::binary_semaphore_array2<1024> locks;

        CHECK(locks.acquire(13));
        CHECK(locks.is_acquired(13));
        CHECK(!locks.acquire(13));
        CHECK(locks.release(13));
        CHECK(!locks.is_acquired(13));
        CHECK(locks.acquire(13));

        //  Acquire then release block of 16 bits - within the first 64 bits

        CHECK(locks.acquire_block(17, 16));
        CHECK(locks.is_acquired(17));
        CHECK(locks.is_acquired(32));

        CHECK(locks.release_block(17, 16));
        CHECK(!locks.is_acquired(17));
        CHECK(!locks.is_acquired(32));

        //  Acquire then release block of 4 bits - spans a uint64_t boundary

        CHECK(locks.acquire_block(62, 4));
        CHECK(!locks.is_acquired(61));
        CHECK(locks.is_acquired(62));
        CHECK(locks.is_acquired(63));
        CHECK(locks.is_acquired(64));
        CHECK(locks.is_acquired(65));
        CHECK(!locks.is_acquired(66));

        CHECK(locks.release_block(62, 4));
        CHECK(!locks.is_acquired(61));
        CHECK(!locks.is_acquired(62));
        CHECK(!locks.is_acquired(63));
        CHECK(!locks.is_acquired(64));
        CHECK(!locks.is_acquired(65));
        CHECK(!locks.is_acquired(66));

        //  Acquire then release block of 16 bits - spans a uint64_t boundary
        //      Check in greater detail.

        CHECK(locks.acquire_block(120, 15));
        CHECK(!locks.is_acquired(119));

        for (int i = 0; i < 15; i++)
        {
            CHECK(locks.is_acquired(120 + i));
        }

        CHECK(!locks.is_acquired(135));
        CHECK(!locks.is_acquired(136));

        locks.acquire(119);
        locks.acquire(135);

        CHECK(locks.release_block(120, 15));
        CHECK(locks.is_acquired(119));

        for (int i = 0; i < 15; i++)
        {
            CHECK(!locks.is_acquired(120 + i));
        }

        CHECK(locks.is_acquired(135));
        CHECK(!locks.is_acquired(136));

        //  Double release is OK

        CHECK(locks.release_block(120, 15));
    }

    TEST(BinarySemaphoreArrayTests, TestSpans)
    {
        minstd::binary_semaphore_array2<16384> semaphores;
        minstd::array<bit_block, 1000> bit_blocks;

        //  Generate a collection of non-overlapping bit blocks and then acquire them.
        //      Check that the bits are acquired and that the bits between the blocks are not acquired.

        size_t num_blocks = generate_non_overlapping_bit_blocks(16384, 12345, bit_blocks);

        for (size_t i = 0; i < num_blocks; i++)
        {
            CHECK(semaphores.acquire_block(bit_blocks[i].start_, bit_blocks[i].length_));
        }

        uint64_t j = 0;

        for (size_t i = 0; i < num_blocks; i++)
        {
            while (j < bit_blocks[i].start_)
            {
                CHECK(!semaphores.is_acquired(j));
                j++;
            }

            while (j < bit_blocks[i].start_ + bit_blocks[i].length_)
            {
                CHECK(semaphores.is_acquired(j));
                j++;
            }
        }

        //  Generate more bit blocks and check to see if they can be acquired.
        //      Then try to acquire and check that those that can be acquired succeed and those that can't fail.

        minstd::array<bit_block, 1000> bit_blocks2;

        size_t num_blocks2 = generate_non_overlapping_bit_blocks(16384, 12345, bit_blocks2);

        for (size_t i = 0; i < num_blocks2; i++)
        {
            bool already_acquired = false;

            for (j = bit_blocks2[i].start_; j < bit_blocks2[i].start_ + bit_blocks2[i].length_; j++)
            {
                if (semaphores.is_acquired(j))
                {
                    already_acquired = true;
                    break;
                }
            }

            if (already_acquired)
            {
                CHECK(!semaphores.acquire_block(bit_blocks2[i].start_, bit_blocks2[i].length_));
            }
            else
            {
                CHECK(semaphores.acquire_block(bit_blocks2[i].start_, bit_blocks2[i].length_));
            }
        }
    }

    TEST(BinarySemaphoreArrayTests, FindNextEmptyBlockTest)
    {
        constexpr size_t SIZE_IN_BITS = 4096;

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(123, 456));

        minstd::binary_semaphore_array2<SIZE_IN_BITS> semaphores;

        for (size_t i = 0; i < SIZE_IN_BITS / 16; i++)
        {
            CHECK(semaphores.acquire_block(i * 16, 16));
        }

        //  Try to find a single bit block, this should fail

        CHECK(semaphores.acquire_next_empty_block(1).has_value() == false);

        //  Release a block and then try to find it

        for (uint32_t j = 0; j < 1000; j++)
        {
            for (uint32_t i = 1; i < 17; i++)
            {
                uint32_t index = rng() % (SIZE_IN_BITS - i);

                CHECK(semaphores.release_block(index, i));

                auto result = semaphores.acquire_next_empty_block(i);

                if (!result.has_value())
                {
                    printf("Round %d Failed to acquire block of size %d at index %d\n", j, i, index);
                }

                CHECK(result.has_value() == true);
                CHECK(result.value() == index);

                //  All bits should be acquired

                result = semaphores.acquire_next_empty_block(1);

                if (result.has_value())
                {
                    printf("Round %d Failed with re-acquire block of size %d at index %d\n", j, i, index);
                }

                CHECK(result.has_value() == false);
            }
        }

        //  Release the last 7 bits and then try to acquire them

        CHECK(semaphores.release_block(SIZE_IN_BITS - 8, 7));

        CHECK(semaphores.acquire_next_empty_block(8).has_value() == false);

        auto result = semaphores.acquire_next_empty_block(7);

        CHECK(result.has_value() == true);
        CHECK(result.value() == SIZE_IN_BITS - 8);
    }

    TEST(BinarySemaphoreArrayTests, MultiThreadedBasicTest)
    {
        constexpr size_t NUM_THREADS = 32;

        minstd::binary_semaphore_array2<16384> semaphores;

        pthread_t threads[NUM_THREADS];
        _thread_arguments arguments[NUM_THREADS];

        start_updates = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            arguments[i].semaphores_ = &semaphores;
            arguments[i].rng_seed_ = i * 100;

            pthread_create(&threads[i], nullptr, _worker_thread1, &arguments[i]);
        }

        start_updates = true;

        //  Wait for all the threads to join

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], nullptr);
        }

        //  Count the number of bits set and compare that to the statistic in the array

        uint32_t bits_set = 0;

        for (size_t i = 0; i < 16384; i++)
        {
            if (semaphores.is_acquired(i))
            {
                bits_set++;
            }
        }

        CHECK_EQUAL(bits_set, semaphores.bits_set());
    }

    TEST(BinarySemaphoreArrayTests, MultiThreadedGetNextBlockTest)
    {
        constexpr size_t SIZE_IN_BITS = 16384;
        constexpr size_t NUM_THREADS = 32;

        minstd::binary_semaphore_array2<SIZE_IN_BITS> semaphores;

        pthread_t threads[NUM_THREADS];
        _thread_arguments arguments[NUM_THREADS];

        start_updates = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            arguments[i].semaphores_ = &semaphores;
            arguments[i].rng_seed_ = i * 200;
            arguments[i].bits_acquired_ = 0;

            pthread_create(&threads[i], nullptr, _worker_thread2, &arguments[i]);
        }

        start_updates = true;

        //  Wait for all the threads to join and sum all the bits acquired across all threads

        auto start = clock();

        uint32_t total_bits_acquired = 0;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], nullptr);

            total_bits_acquired += arguments[i].bits_acquired_;
        }

        auto end = clock();

        auto duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Binary Semaphore Array Multithread Tests Duration: %f\n", duration);

        //  Check that all bits were acquired, up to total_bits_acquired

        if (total_bits_acquired == SIZE_IN_BITS)
        {
            auto result = semaphores.acquire_next_empty_block(1);

            CHECK(result.has_value() == false);
        }
        else
        {
            auto result = semaphores.acquire_next_empty_block(1);

            CHECK(result.has_value() == true);
            CHECK(result.value() == total_bits_acquired);
        }
    }

    //  The benchmarks are interesting but that is about all.  The lockfree behavior of aarch64 is so different
    //      from x64 that generalization here doesn't make much sense.  I have them just to insure that there
    //      is not a huge difference between the two implementations on the x64 platform at least.

    TEST(BinarySemaphoreArrayTests, Benchmark)
    {
        minstd::binary_semaphore_array2<2048> semaphores;

        auto start = clock();

        for (size_t i = 0; i < 5000000; i++)
        {
            size_t block_size = (i % 16) + 1;

            if ((i % 3) == 0)
            {
                semaphores.release_block(i % 2048, block_size);
            }
            else
            {
                semaphores.acquire_next_empty_block(block_size, 0);
            }
        }

        auto end = clock();

        auto duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;

        printf("Binary Semaphore Array Tests Benchmark Duration: %f\n", duration);
    }

    TEST(BinarySemaphoreArrayTests, PerformanceTest)
    {
        constexpr size_t SIZE_IN_BITS = 65536;
        constexpr size_t ITERATIONS_PER_THREAD = 200000;

        minstd::binary_semaphore_array2<SIZE_IN_BITS> semaphores;

        pthread_t threads[32];
        _perf_thread_arguments arguments[32];

        const perf_op ops[] = {perf_op::single_bit, perf_op::block, perf_op::next_block};
        const char *op_names[] = {"Single-Bit", "Block", "Next-Block"};

        for (size_t op_index = 0; op_index < 3; op_index++)
        {
            printf("Binary Semaphore Array %s Ops/sec (Threads 1-32):\n", op_names[op_index]);

            for (size_t num_threads = 1; num_threads <= 32; num_threads++)
            {
                start_updates = false;

                for (size_t i = 0; i < num_threads; i++)
                {
                    arguments[i].semaphores_ = &semaphores;
                    arguments[i].rng_seed_ = (op_index + 1) * 1000 + i * 17;
                    arguments[i].iterations_ = ITERATIONS_PER_THREAD;
                    arguments[i].op_ = ops[op_index];

                    pthread_create(&threads[i], nullptr, _perf_worker, &arguments[i]);
                }

                auto start = clock();
                start_updates = true;

                for (size_t i = 0; i < num_threads; i++)
                {
                    pthread_join(threads[i], nullptr);
                }

                auto end = clock();
                double duration = ((double)(end - start)) / (double)CLOCKS_PER_SEC;
                double ops_per_sec = (double)(ITERATIONS_PER_THREAD * num_threads * 2) / duration;

                printf("  %2zu threads: %f\n", num_threads, ops_per_sec);
            }
        }
    }

}
