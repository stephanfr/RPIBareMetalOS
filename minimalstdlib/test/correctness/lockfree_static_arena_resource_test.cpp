// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>

#include <__memory_resource/lockfree_static_arena_resource.h>

TEST_GROUP(lockfree_static_arena_resource_CorrectnessTests)
{
    void setup() {}
    void teardown() {}
};

TEST(lockfree_static_arena_resource_CorrectnessTests, TestBasicAllocationAndAlignment)
{
    alignas(64) uint8_t buffer[4096];
    minstd::pmr::lockfree_static_arena_resource resource(buffer, sizeof(buffer));

    void* p1 = resource.allocate(32, 8);
    CHECK(p1 != nullptr);
    CHECK_EQUAL(0, reinterpret_cast<uintptr_t>(p1) % 64);

    void* p2 = resource.allocate(16, 16);
    CHECK(p2 != nullptr);
    CHECK_EQUAL(0, reinterpret_cast<uintptr_t>(p2) % 64);
    CHECK(p1 != p2);

    resource.deallocate(p1, 32, 8);
    resource.deallocate(p2, 16, 16);
    
    // Bump allocation continues
    void* p3 = resource.allocate(32, 8);
    CHECK(p3 != nullptr);
    CHECK(p3 != p1 && p3 != p2); // Still bumping
}

TEST(lockfree_static_arena_resource_CorrectnessTests, TestFallbackAllocation)
{
    alignas(64) uint8_t buffer[128]; // Exact size for two blocks (64 each)
    minstd::pmr::lockfree_static_arena_resource resource(buffer, sizeof(buffer));

    void* p1 = resource.allocate(32, 8); // Uses 64 bytes
    void* p2 = resource.allocate(32, 8); // Uses 64 bytes
    
    CHECK(p1 != nullptr);
    CHECK(p2 != nullptr);
    
    // Arena is exhausted now! Next allocation should fail since we hit the end
    // But it will search the free stack, which is empty. So it returns nullptr.
    void* p3 = resource.allocate(32, 8);
    CHECK(p3 == nullptr);

    // Free one block, putting it on the free stack
    resource.deallocate(p1, 32, 8);
    
    // Now allocation should hit the fallback logic and return the freed block
    void* p4 = resource.allocate(32, 8);
    CHECK(p4 == p1);
}

TEST(lockfree_static_arena_resource_CorrectnessTests, TestOversizedAlignmentRejection)
{
    alignas(64) uint8_t buffer[4096];
    minstd::pmr::lockfree_static_arena_resource resource(buffer, sizeof(buffer));

    // Request alignment > MIN_ALIGNMENT (64) -> Should reject by returning nullptr
    void* p = resource.allocate(128, 128);
    CHECK(p == nullptr);
}
