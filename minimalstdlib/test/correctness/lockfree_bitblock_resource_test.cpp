// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>
#include <__memory_resource/lockfree_bitblock_resource.h>
#include <__memory_resource/single_block_resource.h>
#include <stdint.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(LockfreeBitblockResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t buffer_size = 4 * 1048576; // 4 MB
    alignas(64) char test_buffer[buffer_size];
    
    using bitblock_resource_type = minstd::pmr::lockfree_bitblock_resource<64, 1024, 32, 48, false>;
}

TEST(LockfreeBitblockResourceTests, ContiguousScanIntegrity)
{
    minstd::pmr::single_block_resource upstream(test_buffer, buffer_size);
    bitblock_resource_type small_pool(&upstream, 2);

    constexpr size_t max_contiguous_payload = 1008;

    void* max_contiguous_ptr = small_pool.allocate(max_contiguous_payload, 16);
    CHECK(max_contiguous_ptr != nullptr);
    CHECK((uintptr_t)max_contiguous_ptr >= (uintptr_t)test_buffer && (uintptr_t)max_contiguous_ptr < (uintptr_t)test_buffer + buffer_size);

    // Fill the pool to test fragmentation
    void* pointers[1024];
    for (int i = 0; i < 1000; i++) {
        pointers[i] = small_pool.allocate(64, 16);
        CHECK(pointers[i] != nullptr);
    }

    // Free interleaving to cause fragmentation
    for (int i = 0; i < 1000; i += 2) {
        small_pool.deallocate(pointers[i], 64, 16);
    }

    // Try a large aligned alloc—should fail or find another fresh block rather than the fragmented one
    void* large_ptr = small_pool.allocate(max_contiguous_payload, 16);
    CHECK(large_ptr != nullptr);
    CHECK((uintptr_t)large_ptr >= (uintptr_t)test_buffer && (uintptr_t)large_ptr < (uintptr_t)test_buffer + buffer_size);

    // Release the max chunk to let it recombine seamlessly internally over scanning
    small_pool.deallocate(max_contiguous_ptr, max_contiguous_payload, 16);

    // After out of order free, we can still re-allocate
    void* new_max = small_pool.allocate(max_contiguous_payload, 16);
    CHECK(new_max != nullptr);
    CHECK((uintptr_t)new_max >= (uintptr_t)test_buffer && (uintptr_t)new_max < (uintptr_t)test_buffer + buffer_size);

    // Clean up
    small_pool.deallocate(new_max, max_contiguous_payload, 16);
    small_pool.deallocate(large_ptr, max_contiguous_payload, 16);
    
    for (int i = 1; i < 1000; i += 2) {
        small_pool.deallocate(pointers[i], 64, 16);
    }
}
