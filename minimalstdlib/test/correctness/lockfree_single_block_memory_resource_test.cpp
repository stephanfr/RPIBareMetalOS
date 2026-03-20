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

    int counter = 0;

    for (auto iter = resource.begin(); iter != resource.end(); ++iter)
    {
        allocation_info = *iter;

        CHECK(allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE);

        counter++;
    }

    CHECK(counter == 4);

    resource.deallocate(ptr4, 45678);

    counter = 0;

    for (auto iter = resource.begin(); iter != resource.end(); ++iter)
    {
        allocation_info = *iter;

        if (allocation_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE)
        {
            counter++;
        }
    }

    CHECK(counter == 3);

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

TEST(LockfreeSingleBlockMemoryResourceTests, MultiThreadTest)
{
    constexpr size_t NUM_THREADS = 16;

    start_allocations = false;
    exit_thread = false;
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

    allocator_thread_arguments itr_thread_args[5];
    pthread_t itr_threads[5];

    for (size_t i = 0; i < 5; i++)
    {
        itr_thread_args[i].mem_resource = &resource;

        CHECK(pthread_create(&itr_threads[i], NULL, iteration_thread, (void *)&itr_thread_args[i]) == 0);
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

    exit_thread = true;

    for (size_t i = 0; i < 5; i++)
    {
        CHECK(pthread_join(itr_threads[i], NULL) == 0);
    }

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

    total_number_of_allocations = 0;
    total_number_of_bytes_allocated = 0;
    size_t total_number_of_free_blocks = 0;
    size_t total_number_of_bytes_in_free_blocks = 0;

    size_t number_of_list_elements = 0;

    for (auto itr = resource.begin(); itr != resource.end(); ++itr)
    {
        if ((*itr).state != lockfree_single_block_resource_with_stats::allocation_state::IN_USE)
        {
            if ((*itr).state == lockfree_single_block_resource_with_stats::allocation_state::AVAILABLE)
            {
                total_number_of_free_blocks++;
                total_number_of_bytes_in_free_blocks += (*itr).size;
            }

            continue;
        }

        bool found = false;

        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            for (size_t j = 0; j < NUM_ALLOCATIONS_PER_THREAD; j++)
            {
                if ((*itr).location == args[i].pointers_allocated[j])
                {
                    found = true;
                    break;
                }
            }
        }

        CHECK(found);

        number_of_list_elements++;

        CHECK(found);
    }

    //  Deallocate all the allocations

    start_allocations = false;
    exit_thread = false;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        args[i].mem_resource = &resource;
        args[i].rng_seed = i + 333;

        CHECK(pthread_create(&threads[i], NULL, deallocation_thread, (void *)&args[i]) == 0);
    }

    for (size_t i = 0; i < 5; i++)
    {
        itr_thread_args[i].mem_resource = &resource;

        CHECK(pthread_create(&itr_threads[i], NULL, iteration_thread, (void *)&itr_thread_args[i]) == 0);
    }

    start_allocations = true;

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    exit_thread = true;

    for (size_t i = 0; i < 5; i++)
    {
        CHECK(pthread_join(itr_threads[i], NULL) == 0);
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

TEST(LockfreeSingleBlockMemoryResourceTests, ReclaimSoftDeletedMetadataMidListEviction)
{
    lockfree_single_block_resource_without_stats resource(buffer, BUFFER_SIZE);

    void* ptr1 = resource.allocate(64);
    void* ptr2 = resource.allocate(64);
    void* ptr3 = resource.allocate(64);
    void* ptr4 = resource.allocate(64);
    void* ptr5 = resource.allocate(64);

    {
        // Create an active iterator to hold the delete cutoff open
        auto iter = resource.begin();

        // Deallocate mid-list elements while iterator is active
        resource.deallocate(ptr2, 64);
        resource.deallocate(ptr4, 64);

        // Iterator goes out of scope here. The destructor calls reclaim_soft_deleted_metadata()
        // which will walk the soft-deleted list and physically unlink ptr2 and ptr4's metadata.
    }

    void* ptr6 = resource.allocate(64);
    CHECK(ptr6 != nullptr);

    resource.deallocate(ptr1, 64);
    resource.deallocate(ptr3, 64);
    resource.deallocate(ptr5, 64);
    resource.deallocate(ptr6, 64);
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
