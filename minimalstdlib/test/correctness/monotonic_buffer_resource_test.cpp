// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>
#include <__memory_resource/monotonic_buffer_resource.h>
#include <__memory_resource/single_block_resource.h>
#include <stdint.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MonotonicBufferResourceTests)
    {
    };
#pragma GCC diagnostic pop
}

TEST(MonotonicBufferResourceTests, TestConstructors)
{
    // Test provided buffer constructor
    char stack_buf[256];
    minstd::pmr::monotonic_buffer_resource r1(stack_buf, sizeof(stack_buf), nullptr);
    void* p1 = r1.allocate(32, 8);
    CHECK(p1 != nullptr);
    // Should be inside the stack_buf
    CHECK((uintptr_t)p1 >= (uintptr_t)stack_buf);
    CHECK((uintptr_t)p1 < (uintptr_t)stack_buf + sizeof(stack_buf));

    // Test upstream constructor
    char upstream_buf[1024];
    minstd::pmr::single_block_resource upstream(upstream_buf, sizeof(upstream_buf));
    minstd::pmr::monotonic_buffer_resource r2(64, &upstream);

    void* p2 = r2.allocate(16, 8);
    CHECK(p2 != nullptr);
    // It should have allocated from upstream
    CHECK((uintptr_t)p2 >= (uintptr_t)upstream_buf);
    
    // Equal only to itself
    minstd::pmr::monotonic_buffer_resource r3(64, &upstream);
    CHECK(r2 == r2);
    CHECK(r2 != r3);
    CHECK(r1 != r2);
}

TEST(MonotonicBufferResourceTests, TestAllocationAndFailover)
{
    char stack_buf[64];
    char upstream_buf[4096];
    minstd::pmr::single_block_resource upstream(upstream_buf, sizeof(upstream_buf));
    minstd::pmr::monotonic_buffer_resource r(stack_buf, sizeof(stack_buf), &upstream);

    // Initial allocation fits in stack buffer
    void* p1 = r.allocate(32, 16);
    CHECK(p1 != nullptr);
    CHECK((uintptr_t)p1 >= (uintptr_t)stack_buf);
    CHECK((uintptr_t)p1 < (uintptr_t)stack_buf + sizeof(stack_buf));

    // Next allocation will exhaust stack buffer and trigger upstream allocation
    void* p2 = r.allocate(64, 16);
    CHECK(p2 != nullptr);
    CHECK((uintptr_t)p2 >= (uintptr_t)upstream_buf);
    CHECK((uintptr_t)p2 < (uintptr_t)upstream_buf + 4096);

    // Both should still be valid. Deallocate is a no-op so we just call it to ensure no crash
    r.deallocate(p1, 32, 16);
    r.deallocate(p2, 64, 16);
}

TEST(MonotonicBufferResourceTests, TestRelease)
{
    char upstream_buf[4096];
    minstd::pmr::single_block_resource upstream(upstream_buf, sizeof(upstream_buf));
    
    {
        minstd::pmr::monotonic_buffer_resource r(64, &upstream);
        
        r.allocate(128, 8);
        r.allocate(256, 8);
        
        // Before release, memory is in use in upstream
        CHECK(upstream.current_bytes_allocated() > 0);
    } 
    // r goes out of scope, release() is called automatically
    
    // single_block_resource doesn't reclaim memory technically on deallocate,
    // but the monotonic release should have properly forwarded deallocate to it 
    // (We mainly test that it doesn't crash here)
}
