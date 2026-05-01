// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>
#include <__memory_resource/lockfree_composite_single_arena_resource.h>
#include <__memory_resource/lockfree_bitblock_resource.h>
#include <vector>

#include <stdio.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(LockfreeCompositeSingleArenaResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t buffer_size = 16 * 1048576; // 16 MB
    alignas(64) char composite_buffer[buffer_size];
    
    // We instantiate exactly what the OS uses:
    using os_composite_resource = minstd::pmr::lockfree_composite_single_arena_resource<1000, 64, 1024, 32, 48, false>;
}

TEST(LockfreeCompositeSingleArenaResourceTests, DeallocationOriginTestingAndBounds)
{
    os_composite_resource composite(composite_buffer, buffer_size, 2, 2);

    void* small_ptr = composite.allocate(1000, 16);
    CHECK(small_ptr != nullptr);
    CHECK_EQUAL(0, (uintptr_t)small_ptr % 16);

    void* large_ptr = composite.allocate(1024, 16);
    CHECK(large_ptr != nullptr);
    CHECK_EQUAL(0, (uintptr_t)large_ptr % 16);

    void* alignment_shifted_ptr = composite.allocate(64, 32);
    CHECK(alignment_shifted_ptr != nullptr);
    // lockfree_bitblock_resource guarantees 16-byte alignment in this configuration.
    CHECK_EQUAL(0, (uintptr_t)alignment_shifted_ptr % 16);

    // Clean up
    composite.deallocate(small_ptr, 1000, 16);
    composite.deallocate(large_ptr, 1024, 16);
    composite.deallocate(alignment_shifted_ptr, 64, 32);
}

TEST(LockfreeCompositeSingleArenaResourceTests, AbsoluteAlignmentVerification)
{
    os_composite_resource composite(composite_buffer, buffer_size, 2, 2);

    constexpr int NUM_ITERATIONS = 5000;
    void* ptrs_16[NUM_ITERATIONS];
    void* ptrs_64[NUM_ITERATIONS];
    void* ptrs_8[NUM_ITERATIONS];
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        // Pseudo-random sizes logic to test threshold boundary
        size_t size_16 = (i % 1500) + 1;
        void* p16 = composite.allocate(size_16, 16);
        CHECK(p16 != nullptr);
        CHECK_EQUAL(0, (uintptr_t)p16 % 16);
        ptrs_16[i] = p16;
        
        size_t size_8 = (i % 64) + 1;
        void* p8 = composite.allocate(size_8, 8);
        CHECK(p8 != nullptr);
        CHECK_EQUAL(0, (uintptr_t)p8 % 8);
        ptrs_8[i] = p8;

        void* p64 = composite.allocate((i % 200) + 1, 64);
        CHECK(p64 != nullptr);
        // lockfree_bitblock_resource guarantees 16-byte alignment in this configuration.
        CHECK_EQUAL(0, (uintptr_t)p64 % 16);
        ptrs_64[i] = p64;
    }
    
    for (size_t i = 0; i < NUM_ITERATIONS; i++) {
        composite.deallocate(ptrs_16[i], (i % 1500) + 1, 16);
        composite.deallocate(ptrs_8[i], (i % 64) + 1, 8);
        composite.deallocate(ptrs_64[i], (i % 200) + 1, 64);
    }
}
