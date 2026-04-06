// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_block_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include "../shared/interrupt_simulation_test_helpers.h"

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

extern "C" double sqrt(double);
extern "C" double log(double);
extern "C" double cos(double);
extern "C" double exp(double);

namespace
{
    constexpr size_t DEFAULT_ALIGNMENT = alignof(max_align_t);

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char *buffer = new char[BUFFER_SIZE]();

    using lockfree_single_block_resource_debug_metrics =
        minstd::pmr::lockfree_single_block_resource_concrete_debug_metrics<
            test_userspace_signal_mask_interrupt_policy,
            minstd::pmr::platform::default_platform_provider,
            128 * 1024 * 1024,
            5>;

    typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        128 * 1024 * 1024,
        5,

        10,
        lockfree_single_block_resource_debug_metrics,
        minstd::pmr::extensions::memory_resource_statistics,
        minstd::pmr::extensions::hash_check> lockfree_single_block_resource_with_stats;
        
    typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        128 * 1024 * 1024,
        5,

        10,
        lockfree_single_block_resource_debug_metrics,
        minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_block_resource_without_stats;
}


// clang-format off

TEST_GROUP(LockfreeSingleBlockMemoryResourceTests)
{
};

TEST(LockfreeSingleBlockMemoryResourceTests, SingleBlockResourceBasicFunctionality)
{
    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    void *ptr1 = resource.allocate(50);

    CHECK(ptr1 != nullptr);
    CHECK((unsigned long)ptr1 % DEFAULT_ALIGNMENT == 0);

    auto allocation_info = resource.get_allocation_info(ptr1);
    CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 50);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    void *ptr2 = resource.allocate(1023);

    CHECK(ptr2 != nullptr);
    CHECK((unsigned long)ptr2 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr2);
    CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 1023);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    void *ptr3 = resource.allocate(123);

    CHECK(ptr3 != nullptr);
    CHECK((unsigned long)ptr3 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr3);
    CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 123);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    void *ptr4 = resource.allocate(45678);

    CHECK(ptr4 != nullptr);
    CHECK((unsigned long)ptr4 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr4);
    CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 45678);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    CHECK_EQUAL(4, resource.current_allocated());

    resource.deallocate(ptr4, 45678);

    CHECK_EQUAL(3, resource.current_allocated());

    void *ptr5 = resource.allocate(100);

    CHECK(ptr5 != nullptr);
    CHECK((unsigned long)ptr5 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr5);
    CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 100);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);
}

TEST(LockfreeSingleBlockMemoryResourceTests, AllocationLargerThanMaxIsRejected)
{
    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    size_t too_large = lockfree_single_block_resource_with_stats::MAX_ALLOCATION_SIZE + 1;
    void *ptr = resource.allocate(too_large);

    CHECK(ptr == nullptr);
}

TEST(LockfreeSingleBlockMemoryResourceTests, CurrentAllocatedIsTrackedOnTemplateClass)
{
    lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);

    void *ptr1 = resource.allocate(128);
    void *ptr2 = resource.allocate(256);

    CHECK(ptr1 != nullptr);
    CHECK(ptr2 != nullptr);
    CHECK_EQUAL(2, resource.current_allocated());

    resource.deallocate(ptr1, 128);
    CHECK_EQUAL(1, resource.current_allocated());

    resource.deallocate(ptr2, 256);
    CHECK_EQUAL(0, resource.current_allocated());
}

TEST(LockfreeSingleBlockMemoryResourceTests, DeferredDeallocationOpensMaintenanceWindowAtTen)
{
    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 512;
    constexpr size_t NUM_BLOCKS = 10;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    CHECK_EQUAL(NUM_BLOCKS, resource.current_allocated());

    for (size_t i = 0; i < NUM_BLOCKS - 1; ++i)
    {
        resource.deallocate(ptrs[i], ALLOC_SIZE);
    }

    CHECK_EQUAL(1, resource.current_allocated());
    CHECK_EQUAL(static_cast<size_t>(NUM_BLOCKS - 1), resource.debug_pending_deallocations());

    auto pending_info = resource.get_allocation_info(ptrs[0]);
    CHECK(pending_info.state == lockfree_single_block_resource_with_stats::allocation_state::DEALLOCATED_PENDING);

    const size_t windows_before = resource.debug_maintenance_windows();

    resource.deallocate(ptrs[NUM_BLOCKS - 1], ALLOC_SIZE);

    CHECK_EQUAL(0, resource.current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.debug_pending_deallocations());
    CHECK(resource.debug_maintenance_windows() > windows_before);

    auto finalized_info = resource.get_allocation_info(ptrs[0]);
    CHECK(finalized_info.state != lockfree_single_block_resource_with_stats::allocation_state::DEALLOCATED_PENDING);
}

TEST(LockfreeSingleBlockMemoryResourceTests, ReuseAfterMaintenanceKeepsMetadataCountStable)
{
    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 1024;
    constexpr size_t NUM_BLOCKS = 10;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        resource.deallocate(ptrs[i], ALLOC_SIZE);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.debug_pending_deallocations());

    const size_t metadata_before_reuse = resource.debug_metadata_count();

    void *reused = resource.allocate(ALLOC_SIZE);
    CHECK(reused != nullptr);

    const size_t metadata_after_reuse = resource.debug_metadata_count();

    // Full metadata trimming can reset the count to zero; the next allocation then
    // recreates a single metadata record.
    if (metadata_before_reuse == 0)
    {
        CHECK(metadata_after_reuse <= 1);
    }
    else
    {
        CHECK_EQUAL(metadata_before_reuse, metadata_after_reuse);
    }

    auto reused_info = resource.get_allocation_info(reused);
    CHECK(reused_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);

    resource.deallocate(reused, ALLOC_SIZE);
}

TEST(LockfreeSingleBlockMemoryResourceTests, LargePendingBatchDrainsAndOpensMultipleMaintenanceWindows)
{
    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 512;
    constexpr size_t NUM_BLOCKS = 100;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    const size_t windows_before = resource.debug_maintenance_windows();

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        resource.deallocate(ptrs[i], ALLOC_SIZE);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.debug_pending_deallocations());
    CHECK(resource.debug_maintenance_windows() > windows_before);
}

TEST(LockfreeSingleBlockMemoryResourceTests, TailFreeingRestoresFrontierAfterMaintenance)
{
    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 2048;
    constexpr size_t NUM_BLOCKS = 20;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    const size_t initial_frontier = resource.debug_frontier_offset();

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    CHECK(resource.debug_frontier_offset() > initial_frontier);

    // Free in reverse order to maximize contiguous tail reclaim opportunities.
    for (size_t i = NUM_BLOCKS; i > 0; --i)
    {
        resource.deallocate(ptrs[i - 1], ALLOC_SIZE);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.debug_pending_deallocations());
    CHECK_EQUAL(initial_frontier, resource.debug_frontier_offset());
    CHECK_EQUAL(static_cast<size_t>(0), resource.current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.debug_pending_deallocations());
    CHECK_EQUAL(initial_frontier, resource.debug_frontier_offset());
}

struct test_unmasked_interrupt_policy
{
    using interrupt_state_t = uint64_t;
    static inline interrupt_state_t disable_interrupts() { return 0; }
    static inline void restore_interrupts(interrupt_state_t) {}
};

typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy<
    test_unmasked_interrupt_policy,
    minstd::pmr::platform::default_platform_provider,
    128 * 1024 * 1024,
    5,
        128,

    lockfree_single_block_resource_debug_metrics,
    minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_block_resource_unmasked;

static lockfree_single_block_resource_unmasked* s_reentrant_resource = nullptr;
static minstd::atomic<int> s_reentrant_signal_count{0};
static minstd::atomic<bool> s_reentrant_test_done{false};

static void sigusr1_reentrant_alloc_handler(int)
{
    if (s_reentrant_resource != nullptr)
    {
        void* p = s_reentrant_resource->allocate(32);
        if (p)
        {
            s_reentrant_resource->deallocate(p, 32);
            s_reentrant_signal_count.fetch_add(1, minstd::memory_order_seq_cst);
        }
    }
}

static void* bomber_reentrant_fn(void* arg)
{
    pthread_t target_thread = *static_cast<pthread_t*>(arg);
    while (!s_reentrant_test_done.load(minstd::memory_order_acquire))
    {
        pthread_kill(target_thread, SIGUSR1);
        usleep(100);
    }
    return nullptr;
}

TEST(LockfreeSingleBlockMemoryResourceTests, DirectInterruptReentrancy)
{
    lockfree_single_block_resource_unmasked resource(buffer, BUFFER_SIZE);
    
    s_reentrant_resource = &resource;
    s_reentrant_signal_count = 0;
    s_reentrant_test_done.store(false, minstd::memory_order_release);

    struct sigaction sa = {};
    sa.sa_handler = sigusr1_reentrant_alloc_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);

    pthread_t target = pthread_self();
    pthread_t bomber;
    CHECK_EQUAL(0, pthread_create(&bomber, nullptr, bomber_reentrant_fn, &target));

    for (int i = 0; i < 50000; ++i)
    {
        void* p = resource.allocate(64);
        if (p)
        {
            resource.deallocate(p, 64);
        }
    }

    s_reentrant_test_done.store(true, minstd::memory_order_release);
    pthread_join(bomber, nullptr);

    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, nullptr);
    s_reentrant_resource = nullptr;

    CHECK_TRUE(s_reentrant_signal_count > 0);
}
