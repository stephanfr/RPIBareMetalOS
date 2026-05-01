// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_arena_resource.h>
#include <__memory_resource/__extensions/lockfree_single_arena_resource_extended_statistics.h>
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
#include <string.h>

extern "C" double sqrt(double);
extern "C" double log(double);
extern "C" double cos(double);
extern "C" double exp(double);

namespace
{
    constexpr size_t DEFAULT_ALIGNMENT = alignof(max_align_t);

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char *buffer = new char[BUFFER_SIZE]();

    

    typedef minstd::pmr::lockfree_single_arena_resource_impl<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        128 * 1024 * 1024,
        5,

        10,
        minstd::pmr::extensions::lockfree_single_arena_resource_extended_statistics,
        minstd::pmr::extensions::memory_resource_statistics> lockfree_single_arena_resource_with_stats;
        
    typedef minstd::pmr::lockfree_single_arena_resource_impl<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        128 * 1024 * 1024,
        5,

        10,
        minstd::pmr::extensions::lockfree_single_arena_resource_extended_statistics,
        minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_arena_resource_without_stats;
}


// clang-format off

TEST_GROUP(LockfreeSingleArenaMemoryResourceTests)
{
};

TEST(LockfreeSingleArenaMemoryResourceTests, SingleArenaResourceBasicFunctionality)
{
    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    void *ptr1 = resource.allocate(50);

    CHECK(ptr1 != nullptr);
    CHECK((unsigned long)ptr1 % DEFAULT_ALIGNMENT == 0);

    auto allocation_info = resource.get_allocation_info(ptr1);
    CHECK(allocation_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 50);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    void *ptr2 = resource.allocate(1023);

    CHECK(ptr2 != nullptr);
    CHECK((unsigned long)ptr2 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr2);
    CHECK(allocation_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 1023);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    void *ptr3 = resource.allocate(123);

    CHECK(ptr3 != nullptr);
    CHECK((unsigned long)ptr3 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr3);
    CHECK(allocation_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 123);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    void *ptr4 = resource.allocate(45678);

    CHECK(ptr4 != nullptr);
    CHECK((unsigned long)ptr4 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr4);
    CHECK(allocation_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 45678);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);

    CHECK_EQUAL(4, resource.extended_metrics().current_allocated());

    resource.deallocate(ptr4, 45678);

    CHECK_EQUAL(3, resource.extended_metrics().current_allocated());

    void *ptr5 = resource.allocate(100);

    CHECK(ptr5 != nullptr);
    CHECK((unsigned long)ptr5 % DEFAULT_ALIGNMENT == 0);

    allocation_info = resource.get_allocation_info(ptr5);
    CHECK(allocation_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
    CHECK(allocation_info.size == 100);
    CHECK(allocation_info.alignment == DEFAULT_ALIGNMENT);
}

TEST(LockfreeSingleArenaMemoryResourceTests, AllocationLargerThanMaxIsRejected)
{
    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    size_t too_large = lockfree_single_arena_resource_with_stats::MAX_ALLOCATION_SIZE + 1;
    void *ptr = resource.allocate(too_large);

    CHECK(ptr == nullptr);
}

TEST(LockfreeSingleArenaMemoryResourceTests, CurrentAllocatedIsTrackedOnTemplateClass)
{
    lockfree_single_arena_resource_without_stats resource(buffer, BUFFER_SIZE);

    void *ptr1 = resource.allocate(128);
    void *ptr2 = resource.allocate(256);

    CHECK(ptr1 != nullptr);
    CHECK(ptr2 != nullptr);
    CHECK_EQUAL(2, resource.extended_metrics().current_allocated());

    resource.deallocate(ptr1, 128);
    CHECK_EQUAL(1, resource.extended_metrics().current_allocated());

    resource.deallocate(ptr2, 256);
    CHECK_EQUAL(0, resource.extended_metrics().current_allocated());
}

TEST(LockfreeSingleArenaMemoryResourceTests, DeferredDeallocationOpensMaintenanceWindowAtTen)
{
    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 512;
    constexpr size_t NUM_BLOCKS = 10;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    CHECK_EQUAL(NUM_BLOCKS, resource.extended_metrics().current_allocated());

    for (size_t i = 0; i < NUM_BLOCKS - 1; ++i)
    {
        resource.deallocate(ptrs[i], ALLOC_SIZE);
    }

    CHECK_EQUAL(1, resource.extended_metrics().current_allocated());
    CHECK_EQUAL(static_cast<size_t>(NUM_BLOCKS - 1), resource.extended_metrics().pending_deallocations());

    auto pending_info = resource.get_allocation_info(ptrs[0]);
    CHECK(pending_info.state == lockfree_single_arena_resource_with_stats::allocation_state::DEALLOCATED_PENDING);

    const size_t windows_before = resource.extended_metrics().maintenance_windows();

    resource.deallocate(ptrs[NUM_BLOCKS - 1], ALLOC_SIZE);

    CHECK_EQUAL(0, resource.extended_metrics().current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().pending_deallocations());
    CHECK(resource.extended_metrics().maintenance_windows() > windows_before);

    auto finalized_info = resource.get_allocation_info(ptrs[0]);
    CHECK(finalized_info.state != lockfree_single_arena_resource_with_stats::allocation_state::DEALLOCATED_PENDING);
}

TEST(LockfreeSingleArenaMemoryResourceTests, ReuseAfterMaintenanceKeepsMetadataCountStable)
{
    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

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

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().pending_deallocations());

    const size_t metadata_before_reuse = resource.extended_metrics().metadata_count();

    void *reused = resource.allocate(ALLOC_SIZE);
    CHECK(reused != nullptr);

    const size_t metadata_after_reuse = resource.extended_metrics().metadata_count();

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
    CHECK(reused_info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);

    resource.deallocate(reused, ALLOC_SIZE);
}

TEST(LockfreeSingleArenaMemoryResourceTests, LargePendingBatchDrainsAndOpensMultipleMaintenanceWindows)
{
    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 512;
    constexpr size_t NUM_BLOCKS = 100;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    const size_t windows_before = resource.extended_metrics().maintenance_windows();

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        resource.deallocate(ptrs[i], ALLOC_SIZE);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().pending_deallocations());
    CHECK(resource.extended_metrics().maintenance_windows() > windows_before);
}

TEST(LockfreeSingleArenaMemoryResourceTests, TailFreeingRestoresFrontierAfterMaintenance)
{
    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 2048;
    constexpr size_t NUM_BLOCKS = 20;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    const size_t initial_frontier = resource.extended_metrics().frontier_offset();

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    CHECK(resource.extended_metrics().frontier_offset() > initial_frontier);

    // Free in reverse order to maximize contiguous tail reclaim opportunities.
    for (size_t i = NUM_BLOCKS; i > 0; --i)
    {
        resource.deallocate(ptrs[i - 1], ALLOC_SIZE);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());
    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().pending_deallocations());
    CHECK_EQUAL(initial_frontier, resource.extended_metrics().frontier_offset());
}

struct test_unmasked_interrupt_policy
{
    using interrupt_state_t = uint64_t;
    static inline interrupt_state_t disable_interrupts() { return 0; }
    static inline void restore_interrupts(interrupt_state_t) {}
};

typedef minstd::pmr::lockfree_single_arena_resource_impl<
    test_unmasked_interrupt_policy,
    minstd::pmr::platform::default_platform_provider,
    128 * 1024 * 1024,
    5,
        128,

    minstd::pmr::extensions::lockfree_single_arena_resource_extended_statistics,
    minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_arena_resource_unmasked;

static lockfree_single_arena_resource_unmasked* s_reentrant_resource = nullptr;
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

TEST(LockfreeSingleArenaMemoryResourceTests, DirectInterruptReentrancy)
{
    lockfree_single_arena_resource_unmasked resource(buffer, BUFFER_SIZE);
    
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

// ---------------------------------------------------------------------------
// Multi-threaded concurrency correctness tests
// ---------------------------------------------------------------------------

TEST(LockfreeSingleArenaMemoryResourceTests, ConcurrentDoubleFreeResilience)
{
    //  Multiple threads attempt to deallocate the same pointer simultaneously.
    //  Exactly one should succeed; no crashes, no corruption.

    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 256;
    constexpr size_t NUM_THREADS = 8;
    constexpr size_t NUM_ROUNDS = 5000;

    struct round_args
    {
        minstd::pmr::memory_resource *resource;
        void *ptr;
        size_t size;
        minstd::atomic<size_t> *success_count;
        minstd::atomic<bool> *go;
    };

    auto double_free_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<round_args *>(arg);

        while (!a->go->load(minstd::memory_order_acquire))
        {
            // spin
        }

        a->resource->deallocate(a->ptr, a->size);

        //  After deallocation, the block should no longer be IN_USE.
        //  The allocator may reject the duplicate dealloc silently.
        //  We count how many threads saw the block still IN_USE before their CAS.
        //  (We cannot directly observe CAS success from the public API, but
        //   the resource must not crash or corrupt internal state.)
        return nullptr;
    };

    for (size_t round = 0; round < NUM_ROUNDS; ++round)
    {
        void *ptr = resource.allocate(ALLOC_SIZE);
        CHECK(ptr != nullptr);

        auto info = resource.get_allocation_info(ptr);
        CHECK(info.state == lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);

        minstd::atomic<size_t> success_count{0};
        minstd::atomic<bool> go{false};

        round_args args[NUM_THREADS];
        pthread_t threads[NUM_THREADS];

        for (size_t t = 0; t < NUM_THREADS; ++t)
        {
            args[t] = {&resource, ptr, ALLOC_SIZE, &success_count, &go};
            CHECK_EQUAL(0, pthread_create(&threads[t], nullptr, double_free_fn, &args[t]));
        }

        go.store(true, minstd::memory_order_release);

        for (size_t t = 0; t < NUM_THREADS; ++t)
        {
            pthread_join(threads[t], nullptr);
        }

        //  After all threads finish, the allocation must not be IN_USE
        info = resource.get_allocation_info(ptr);
        CHECK(info.state != lockfree_single_arena_resource_with_stats::allocation_state::IN_USE);
    }

    //  Final consistency: no live allocations remain
    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());
}

TEST(LockfreeSingleArenaMemoryResourceTests, ConcurrentAllocDeallocChurn)
{
    //  Multiple threads perform rapid alloc/dealloc cycles concurrently.
    //  Validates that reuse paths and frontier paths are both exercised
    //  without corruption.

    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t NUM_THREADS = 16;
    constexpr size_t CYCLES_PER_THREAD = 10000;

    struct churn_args
    {
        minstd::pmr::memory_resource *resource;
        minstd::atomic<bool> *go;
        size_t thread_id;
        size_t failures;

        churn_args() : resource(nullptr), go(nullptr), thread_id(0), failures(0) {}
    };

    auto churn_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<churn_args *>(arg);

        while (!a->go->load(minstd::memory_order_acquire))
        {
            // spin
        }

        //  Use a simple LCG seeded per-thread
        uint64_t rng_state = a->thread_id * 6364136223846793005ULL + 1442695040888963407ULL;
        auto next_rng = [&rng_state]() -> uint32_t
        {
            rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<uint32_t>(rng_state >> 33);
        };

        for (size_t i = 0; i < CYCLES_PER_THREAD; ++i)
        {
            size_t alloc_size = 64 + (next_rng() % 4096);
            void *ptr = a->resource->allocate(alloc_size);
            if (ptr == nullptr)
            {
                a->failures++;
                continue;
            }

            //  Write a canary pattern to detect memory corruption
            auto *bytes = static_cast<unsigned char *>(ptr);
            unsigned char pattern = static_cast<unsigned char>(a->thread_id ^ (i & 0xFF));
            bytes[0] = pattern;
            bytes[alloc_size - 1] = pattern;

            //  Occasionally hold the allocation briefly
            if ((next_rng() % 8) == 0)
            {
                for (volatile int spin = 0; spin < 10; ++spin)
                {
                }
            }

            //  Verify canary is intact before deallocating
            CHECK_EQUAL(pattern, bytes[0]);
            CHECK_EQUAL(pattern, bytes[alloc_size - 1]);

            a->resource->deallocate(ptr, alloc_size);
        }

        return nullptr;
    };

    minstd::atomic<bool> go{false};
    churn_args args[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        args[t].resource = &resource;
        args[t].go = &go;
        args[t].thread_id = t + 1;
        CHECK_EQUAL(0, pthread_create(&threads[t], nullptr, churn_fn, &args[t]));
    }

    go.store(true, minstd::memory_order_release);

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        pthread_join(threads[t], nullptr);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());

    //  Verify reuse path was actually exercised
    CHECK(resource.extended_metrics().allocator_reuse_hits() > 0);
}

TEST(LockfreeSingleArenaMemoryResourceTests, ExhaustAndRecoverUnderContention)
{
    //  Drive the allocator to exhaustion from multiple threads, then
    //  have all threads deallocate and confirm the resource recovers fully.

    constexpr size_t SMALL_BUFFER_SIZE = 16 * 1048576; // 16 MB
    char *small_buffer = static_cast<char *>(malloc(SMALL_BUFFER_SIZE));
    memset(small_buffer, 0, SMALL_BUFFER_SIZE);

    typedef minstd::pmr::lockfree_single_arena_resource_impl<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        16 * 1024 * 1024,
        5,
        10,
        minstd::pmr::extensions::lockfree_single_arena_resource_extended_statistics,
        minstd::pmr::extensions::memory_resource_statistics> small_resource_type;

    small_resource_type resource(small_buffer, SMALL_BUFFER_SIZE);

    constexpr size_t NUM_THREADS = 8;
    constexpr size_t ALLOCS_PER_THREAD = 600;
    constexpr size_t ALLOC_SIZE = 4096;

    struct exhaust_args
    {
        minstd::pmr::memory_resource *resource;
        minstd::atomic<bool> *go;
        void *ptrs[ALLOCS_PER_THREAD];
        size_t count;
        size_t failures;

        exhaust_args() : resource(nullptr), go(nullptr), count(0), failures(0)
        {
            for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
            {
                ptrs[i] = nullptr;
            }
        }
    };

    auto exhaust_alloc_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<exhaust_args *>(arg);

        while (!a->go->load(minstd::memory_order_acquire))
        {
            // spin
        }

        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            a->ptrs[i] = a->resource->allocate(ALLOC_SIZE);
            if (a->ptrs[i] == nullptr)
            {
                a->failures++;
            }
            else
            {
                a->count++;
            }
        }
        return nullptr;
    };

    auto exhaust_dealloc_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<exhaust_args *>(arg);

        while (!a->go->load(minstd::memory_order_acquire))
        {
            // spin
        }

        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            if (a->ptrs[i] != nullptr)
            {
                a->resource->deallocate(a->ptrs[i], ALLOC_SIZE);
            }
        }
        return nullptr;
    };

    //  Phase 1: Allocate to exhaustion
    minstd::atomic<bool> go{false};
    exhaust_args args[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        args[t].resource = &resource;
        args[t].go = &go;
        CHECK_EQUAL(0, pthread_create(&threads[t], nullptr, exhaust_alloc_fn, &args[t]));
    }

    go.store(true, minstd::memory_order_release);

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        pthread_join(threads[t], nullptr);
    }

    //  At least some allocations should have failed (exhaustion)
    size_t total_failures = 0;
    size_t total_allocated = 0;
    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        total_failures += args[t].failures;
        total_allocated += args[t].count;
    }
    CHECK(total_failures > 0);
    CHECK(total_allocated > 0);
    CHECK_EQUAL(total_allocated, resource.extended_metrics().current_allocated());

    //  Phase 2: Deallocate all from multiple threads concurrently
    minstd::atomic<bool> go2{false};
    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        args[t].go = &go2;
        CHECK_EQUAL(0, pthread_create(&threads[t], nullptr, exhaust_dealloc_fn, &args[t]));
    }

    go2.store(true, minstd::memory_order_release);

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        pthread_join(threads[t], nullptr);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());

    //  Phase 3: Confirm recovery — fresh allocations should succeed
    void *recovery_ptr = resource.allocate(ALLOC_SIZE);
    CHECK(recovery_ptr != nullptr);
    resource.deallocate(recovery_ptr, ALLOC_SIZE);

    free(small_buffer);
}

TEST(LockfreeSingleArenaMemoryResourceTests, CrossThreadDeallocation)
{
    //  Thread A allocates, Thread B deallocates — validates that blocks
    //  allocated on one thread can be safely deallocated on another.

    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t NUM_THREADS = 8;
    constexpr size_t ALLOCS_PER_THREAD = 2000;

    struct cross_thread_args
    {
        minstd::pmr::memory_resource *resource;
        minstd::atomic<bool> *go;
        void *ptrs[ALLOCS_PER_THREAD];
        size_t sizes[ALLOCS_PER_THREAD];
        size_t count;
        minstd::atomic<bool> alloc_done;

        cross_thread_args() : resource(nullptr), go(nullptr), count(0), alloc_done(false)
        {
            for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
            {
                ptrs[i] = nullptr;
                sizes[i] = 0;
            }
        }
    };

    auto allocator_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<cross_thread_args *>(arg);

        while (!a->go->load(minstd::memory_order_acquire))
        {
            // spin
        }

        uint64_t rng_state = reinterpret_cast<uint64_t>(arg);
        auto next_rng = [&rng_state]() -> uint32_t
        {
            rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<uint32_t>(rng_state >> 33);
        };

        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            size_t sz = 64 + (next_rng() % 2048);
            void *p = a->resource->allocate(sz);
            if (p != nullptr)
            {
                a->ptrs[i] = p;
                a->sizes[i] = sz;
                a->count++;
            }
        }

        a->alloc_done.store(true, minstd::memory_order_release);
        return nullptr;
    };

    auto deallocator_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<cross_thread_args *>(arg);

        //  Wait for allocator thread to finish
        while (!a->alloc_done.load(minstd::memory_order_acquire))
        {
            // spin
        }

        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            if (a->ptrs[i] != nullptr)
            {
                a->resource->deallocate(a->ptrs[i], a->sizes[i]);
            }
        }
        return nullptr;
    };

    minstd::atomic<bool> go{false};
    cross_thread_args args[NUM_THREADS];
    pthread_t alloc_threads[NUM_THREADS];
    pthread_t dealloc_threads[NUM_THREADS];

    //  Start allocator threads and corresponding deallocator threads
    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        args[t].resource = &resource;
        args[t].go = &go;
        CHECK_EQUAL(0, pthread_create(&alloc_threads[t], nullptr, allocator_fn, &args[t]));
        CHECK_EQUAL(0, pthread_create(&dealloc_threads[t], nullptr, deallocator_fn, &args[t]));
    }

    go.store(true, minstd::memory_order_release);

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        pthread_join(alloc_threads[t], nullptr);
        pthread_join(dealloc_threads[t], nullptr);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());
}

TEST(LockfreeSingleArenaMemoryResourceTests, FrontierReclaimRace)
{
    //  Allocate exactly in order, then deallocate the tail blocks from multiple
    //  threads to stress the frontier reclaim path (try_reclaim_frontier_blocks).

    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t NUM_THREADS = 8;
    constexpr size_t BLOCKS_PER_THREAD = 13;
    constexpr size_t BACK_HALF = NUM_THREADS * BLOCKS_PER_THREAD;  // 104
    constexpr size_t NUM_BLOCKS = BACK_HALF * 2;                    // 208
    constexpr size_t ALLOC_SIZE = 1024;
    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t i = 0; i < NUM_BLOCKS; ++i)
    {
        ptrs[i] = resource.allocate(ALLOC_SIZE);
        CHECK(ptrs[i] != nullptr);
    }

    const size_t peak_frontier = resource.extended_metrics().frontier_offset();
    CHECK(peak_frontier > 0);

    //  Free the first half single-threaded (these become binned, not frontier-reclaimable)
    for (size_t i = 0; i < BACK_HALF; ++i)
    {
        resource.deallocate(ptrs[i], ALLOC_SIZE);
    }

    //  Free the second half from multiple threads simultaneously
    //  These are at the tail of the arena and should trigger frontier reclaim races.

    struct frontier_args
    {
        minstd::pmr::memory_resource *resource;
        minstd::atomic<bool> *go;
        void **ptrs;
        size_t count;
    };

    auto frontier_dealloc_fn = [](void *arg) -> void *
    {
        auto *a = static_cast<frontier_args *>(arg);

        while (!a->go->load(minstd::memory_order_acquire))
        {
            // spin
        }

        for (size_t i = 0; i < a->count; ++i)
        {
            if (a->ptrs[i] != nullptr)
            {
                a->resource->deallocate(a->ptrs[i], ALLOC_SIZE);
            }
        }
        return nullptr;
    };

    minstd::atomic<bool> go{false};
    frontier_args args[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        size_t start = BACK_HALF + t * BLOCKS_PER_THREAD;
        args[t] = {&resource, &go, &ptrs[start], BLOCKS_PER_THREAD};
        CHECK_EQUAL(0, pthread_create(&threads[t], nullptr, frontier_dealloc_fn, &args[t]));
    }

    go.store(true, minstd::memory_order_release);

    for (size_t t = 0; t < NUM_THREADS; ++t)
    {
        pthread_join(threads[t], nullptr);
    }

    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());

    //  Trigger maintenance to drain any remaining pending deallocations
    void *drain_ptr = resource.allocate(ALLOC_SIZE);
    if (drain_ptr != nullptr)
    {
        resource.deallocate(drain_ptr, ALLOC_SIZE);
    }

    //  Frontier should have been reclaimed at least partially
    CHECK(resource.extended_metrics().frontier_offset() < peak_frontier);
}

TEST(LockfreeSingleArenaMemoryResourceTests, SignalDuringDeallocation)
{
    //  Exercise the allocator with SIGUSR1 bombing specifically during
    //  the deallocation path (not just during allocation as DirectInterruptReentrancy tests).
    //  The signal handler performs allocation + deallocation to provoke reentrancy.

    lockfree_single_arena_resource_unmasked resource(buffer, BUFFER_SIZE);

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

    //  Perform many alloc/dealloc cycles so that signals land during both paths
    constexpr size_t NUM_CYCLES = 50000;
    constexpr size_t BATCH_SIZE = 32;
    void *batch[BATCH_SIZE] = {nullptr};

    for (size_t i = 0; i < NUM_CYCLES; ++i)
    {
        //  Allocate a batch
        size_t count = 0;
        for (size_t b = 0; b < BATCH_SIZE; ++b)
        {
            batch[b] = resource.allocate(64 + (i % 256));
            if (batch[b] != nullptr)
            {
                count++;
            }
        }

        //  Deallocate in reverse order to maximize overlap with signal-handler allocations
        for (size_t b = count; b > 0; --b)
        {
            if (batch[b - 1] != nullptr)
            {
                resource.deallocate(batch[b - 1], 64 + (i % 256));
                batch[b - 1] = nullptr;
            }
        }
    }

    s_reentrant_test_done.store(true, minstd::memory_order_release);
    pthread_join(bomber, nullptr);

    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, nullptr);
    s_reentrant_resource = nullptr;

    CHECK_TRUE(s_reentrant_signal_count > 0);
    CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());
}

TEST(LockfreeSingleArenaMemoryResourceTests, MetadataTrimStress)
{
    //  Repeatedly exhaust and release all blocks to stress the metadata trim
    //  path.  Metadata count should remain bounded.

    lockfree_single_arena_resource_with_stats resource(buffer, BUFFER_SIZE);

    constexpr size_t ALLOC_SIZE = 2048;
    constexpr size_t NUM_BLOCKS = 500;
    constexpr size_t ROUNDS = 20;

    void *ptrs[NUM_BLOCKS] = {nullptr};

    for (size_t round = 0; round < ROUNDS; ++round)
    {
        //  Allocate a large batch
        for (size_t i = 0; i < NUM_BLOCKS; ++i)
        {
            ptrs[i] = resource.allocate(ALLOC_SIZE);
            CHECK(ptrs[i] != nullptr);
        }

        CHECK_EQUAL(NUM_BLOCKS, resource.extended_metrics().current_allocated());

        //  Free in reverse to trigger frontier reclaim + metadata recycling
        for (size_t i = NUM_BLOCKS; i > 0; --i)
        {
            resource.deallocate(ptrs[i - 1], ALLOC_SIZE);
        }

        CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().current_allocated());
        CHECK_EQUAL(static_cast<size_t>(0), resource.extended_metrics().pending_deallocations());
    }

    //  Metadata count should not have grown unboundedly
    size_t final_metadata = resource.extended_metrics().metadata_count();
    CHECK(final_metadata <= NUM_BLOCKS);
}
