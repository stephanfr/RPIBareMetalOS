// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../shared/lockfree_single_block_resource_test_helpers.h"

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
}

TEST(LockfreeSingleBlockMemoryResourceTests, MultiThreadTest)
{
    constexpr size_t NUM_THREADS = 16;

    start_allocations = false;
    correctness_allocation_failed.store(false, minstd::memory_order_release);

    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);

    allocator_thread_arguments args[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &resource;
        args[i].rng_seed = i + 333;
        args[i].reduced_pressure_for_correctness = true;

        CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
    }

    sleep(1);

    start_allocations = true;

    timespec start_time{};
    timespec end_time{};
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    CHECK_FALSE(correctness_allocation_failed.load(minstd::memory_order_acquire));

    double duration = (end_time.tv_sec - start_time.tv_sec) +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Lockfree Single Block Resource Multithread Tests Duration: %f\n", duration);

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

            if (!args[i].deleted_element[j])
            {
                auto alloc_info = resource.get_allocation_info(args[i].pointers_allocated[j]);

                if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::INVALID)
                {
                    printf("Invalid allocation info\n");
                }

                CHECK(alloc_info.state != lockfree_single_block_resource_with_stats::allocation_state::INVALID);

                total_number_of_allocations++;
                total_number_of_bytes_allocated += args[i].sizes_allocated[j];
            }
        }
    }

    CHECK_EQUAL(total_number_of_allocations, resource.current_allocated());
    CHECK_EQUAL(total_number_of_bytes_allocated, resource.current_bytes_allocated());

    //  Verify each allocation can be looked up via get_allocation_info

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        for (size_t j = 0; j < NUM_ALLOCATIONS_PER_THREAD; j++)
        {
            if (args[i].pointers_allocated[j] == nullptr)
            {
                break;
            }

            if (!args[i].deleted_element[j])
            {
                auto alloc_info = resource.get_allocation_info(args[i].pointers_allocated[j]);
                CHECK(alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);
            }
        }
    }

    //  Deallocate all the allocations

    start_allocations = false;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &resource;
        args[i].rng_seed = i + 333;

        CHECK(pthread_create(&threads[i], NULL, deallocation_thread, (void *)&args[i]) == 0);
    }

    start_allocations = true;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    CHECK_EQUAL(0, resource.current_allocated());
    CHECK_EQUAL(0, resource.current_bytes_allocated());

    //  Again with malloc/free

    start_allocations = false;

    minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &malloc_free_resource;

        CHECK(pthread_create(&threads[i], NULL, allocation_thread, (void *)&args[i]) == 0);
    }

    sleep(1);

    start_allocations = true;

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    duration = (end_time.tv_sec - start_time.tv_sec) +
               (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Malloc Free Resource Multithread Tests Duration: %f\n", duration);
}

TEST(LockfreeSingleBlockMemoryResourceTests, InterruptRobustness)
{
    lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);
    s_intr_resource = &resource;
    s_intr_signal_count = 0;
    s_intr_nested_count = 0;
    s_intr_pending_ops = 0;

    struct sigaction sa = {};
    sa.sa_handler = sigusr1_nested_alloc_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);

    minstd::atomic<bool> stop_flag{false};
    intr_test_bomber_args args{pthread_self(), &stop_flag};
    pthread_t bomber;
    CHECK_EQUAL(0, pthread_create(&bomber, nullptr, intr_test_bomber_fn, &args));

    for (int i = 0; i < 50000; ++i)
    {
        process_pending_intr_work(&resource);

        void* p = guarded_allocate(&resource, 32);
        if (p)
        {
            guarded_deallocate(&resource, p, 32);
        }
    }

    drain_pending_intr_work(&resource);

    stop_flag.store(true, minstd::memory_order_release);
    pthread_join(bomber, nullptr);

    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, nullptr);
    s_intr_resource = nullptr;

    CHECK_TRUE(s_intr_signal_count > 0);
    CHECK_TRUE(s_intr_nested_count > 0);
}
