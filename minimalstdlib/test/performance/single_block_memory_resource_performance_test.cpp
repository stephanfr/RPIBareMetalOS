// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>
#include <__memory_resource/single_block_resource.h>

#include <array>
#include <random>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(SingleBlockMemoryResourcePerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t buffer_size = 64 * 1048576; // 64 MB
    char buffer[buffer_size];
}

//  The benchmarks are interesting but that is about all.  The lockfree behavior of aarch64 is so different
//      from x64 that generalization here doesn't make much sense.  I have them just to insure that there
//      is not a huge difference between the two implementations on the x64 platform at least.

TEST(SingleBlockMemoryResourcePerformanceTests, Benchmark)
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

    auto start_sbmr = clock();

    for (size_t i = 0; i < NUM_ALLOCATIONS; i++)
    {
        pointers[i] = resource.allocate(sizes[i]);
    }

    auto end_sbmr = clock();

    auto duration_smbr = ((double)(end_sbmr - start_sbmr)) / (double)CLOCKS_PER_SEC;

    printf("\nDuration SBMR: %f\n", duration_smbr);

    auto start_malloc = clock();

    for (size_t i = 0; i < NUM_ALLOCATIONS; i++)
    {
        pointers[i] = malloc(sizes[i]);
    }

    auto end_malloc = clock();

    auto duration_malloc = ((double)(end_malloc - start_malloc)) / (double)CLOCKS_PER_SEC;

    printf("Duration malloc: %f\n", duration_malloc);

    // CHECK(duration_smbr < (duration_malloc * 2));     //  Worst case, the SMBR test timing should be no more than 2 times longer than malloc
}
