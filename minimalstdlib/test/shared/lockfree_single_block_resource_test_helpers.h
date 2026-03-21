// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_block_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include "os_abstractions.h"

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
    struct test_userspace_signal_mask_interrupt_policy
    {
        using interrupt_state_t = uint64_t;

        static inline interrupt_state_t disable_interrupts()
        {
            return minstd::pmr::test::os_abstractions::enter_critical_section();
        }

        static inline void restore_interrupts(interrupt_state_t /* state */)
        {
            minstd::pmr::test::os_abstractions::leave_critical_section();
        }
    };

    constexpr size_t DEFAULT_ALIGNMENT = alignof(max_align_t);

    constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 5000;

    constexpr size_t PERF_ALLOCATIONS_PER_THREAD = 2000;
    constexpr size_t PERF_MAX_ALLOCATION_SIZE = 16384;
    constexpr size_t PERF_REPETITIONS = 500;

    size_t lognormal_sample(minstd::Xoroshiro128PlusPlusRNG &rng)
    {
        double u1, u2;
        do
        {
            u1 = static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
            u2 = static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
        } while (u1 == 0.0);

        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
        double value = exp(5.4 + 1.2 * z);
        return (value < 1.0) ? 1 : static_cast<size_t>(value);
    }

    constexpr size_t BUFFER_SIZE = 512 * 1048576; // 512 MB
    char *buffer = new char[BUFFER_SIZE]();

    minstd::atomic<bool> start_allocations = false;
    minstd::atomic<bool> exit_thread = false;
    minstd::atomic<bool> correctness_allocation_failed = false;

    typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        32 * 1024 * 1024,
        5,
        minstd::pmr::extensions::memory_resource_statistics,
        minstd::pmr::extensions::hash_check> lockfree_single_block_resource_with_stats;
    typedef minstd::pmr::lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy<
        test_userspace_signal_mask_interrupt_policy,
        minstd::pmr::platform::default_platform_provider,
        32 * 1024 * 1024,
        5,
        minstd::pmr::extensions::null_memory_resource_statistics> lockfree_single_block_resource_without_stats;

    struct userspace_signal_guard
    {
        userspace_signal_guard()
        {
            minstd::pmr::test::os_abstractions::enter_critical_section();
        }

        ~userspace_signal_guard()
        {
            minstd::pmr::test::os_abstractions::leave_critical_section();
        }
    };

    inline void *guarded_allocate(minstd::pmr::memory_resource *resource, size_t bytes)
    {
        userspace_signal_guard guard;
        return resource->allocate(bytes);
    }

    inline void guarded_deallocate(minstd::pmr::memory_resource *resource, void *ptr, size_t bytes)
    {
        userspace_signal_guard guard;
        resource->deallocate(ptr, bytes);
    }

    struct allocator_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        bool reduced_pressure_for_correctness = false;
        minstd::array<void *, NUM_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deleted_element = {false};
        size_t repetitions = 0;
    };

    struct perf_thread_arguments
    {
        minstd::pmr::memory_resource *mem_resource;
        uint64_t rng_seed;
        minstd::array<void *, PERF_ALLOCATIONS_PER_THREAD> pointers_allocated = {nullptr};
        minstd::array<size_t, PERF_ALLOCATIONS_PER_THREAD> sizes_allocated = {0};
        size_t repetitions = 0;
    };

    void *allocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> sizes;
        minstd::array<bool, NUM_ALLOCATIONS_PER_THREAD> deallocate_operation = {false};
        minstd::array<size_t, NUM_ALLOCATIONS_PER_THREAD> deallocation_index = {0};

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->reduced_pressure_for_correctness)
            {
                sizes[i] = 256 + (rng() % 7000);
            }
            else
            {
                sizes[i] = 256 + (rng() % 18000);
            }
        }

        for (size_t i = 50; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            deallocate_operation[i] = ((rng() % 4) == 0);

            if (deallocate_operation[i])
            {
                deallocation_index[i] = rng() % (i + 1);
            }
        }

        while (!start_allocations)
        {
            sched_yield();
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            void *ptr = args->mem_resource->allocate(sizes[i]);

            if (ptr == nullptr)
            {
                printf("thread got nullptr");
                correctness_allocation_failed.store(true, minstd::memory_order_release);
                return nullptr;
            }

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = sizes[i];

            if (deallocate_operation[i])
            {
                if (!args->deleted_element[deallocation_index[i]])
                {
                    args->mem_resource->deallocate(args->pointers_allocated[deallocation_index[i]], args->sizes_allocated[deallocation_index[i]]);

                    args->deleted_element[deallocation_index[i]] = true;
                }
            }
        }

        return nullptr;
    }

    void *deallocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        while (!start_allocations)
        {
            sched_yield();
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            if ((args->pointers_allocated[i] != nullptr) && !args->deleted_element[i])
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    void *repeated_allocation_deallocation_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        while (!start_allocations)
        {
            sched_yield();
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t block_size = 128 + (rng() % 7000);

            void *ptr = args->mem_resource->allocate(block_size);

            if (ptr == nullptr)
            {
                printf("thread got nullptr");
                return nullptr;
            }

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = block_size;
        }

        for (size_t j = 0; j < args->repetitions; j++)
        {
            for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);

                size_t block_size = 128 + (rng() % 7000);

                void *ptr = args->mem_resource->allocate(block_size);

                if (ptr == nullptr)
                {
                    printf("thread got nullptr");
                    return nullptr;
                }

                args->pointers_allocated[i] = ptr;
                args->sizes_allocated[i] = block_size;
            }
        }

        for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    void *perf_repeated_allocation_deallocation_thread(void *arguments)
    {
        perf_thread_arguments *args = static_cast<perf_thread_arguments *>(arguments);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        while (!start_allocations)
        {
            sched_yield();
        }

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            size_t block_size = lognormal_sample(rng);
            if (block_size > PERF_MAX_ALLOCATION_SIZE)
                block_size = PERF_MAX_ALLOCATION_SIZE;

            void *ptr = args->mem_resource->allocate(block_size);

            if (ptr == nullptr)
            {
                printf("thread got nullptr\n");
                return nullptr;
            }

            args->pointers_allocated[i] = ptr;
            args->sizes_allocated[i] = block_size;
        }

        for (size_t j = 0; j < args->repetitions; j++)
        {
            for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);

                size_t block_size = lognormal_sample(rng);
                if (block_size > PERF_MAX_ALLOCATION_SIZE)
                    block_size = PERF_MAX_ALLOCATION_SIZE;

                void *ptr = args->mem_resource->allocate(block_size);

                if (ptr == nullptr)
                {
                    printf("thread got nullptr\n");
                    return nullptr;
                }

                args->pointers_allocated[i] = ptr;
                args->sizes_allocated[i] = block_size;
            }
        }

        for (size_t i = 0; i < PERF_ALLOCATIONS_PER_THREAD; i++)
        {
            if (args->pointers_allocated[i] != nullptr)
            {
                args->mem_resource->deallocate(args->pointers_allocated[i], args->sizes_allocated[i]);
            }
        }

        return nullptr;
    }

    void *iteration_thread(void *arguments)
    {
        allocator_thread_arguments *args = static_cast<allocator_thread_arguments *>(arguments);

        while (!start_allocations)
        {
            sched_yield();
        }

        while (!exit_thread)
        {
            size_t in_use = 0;
            size_t available = 0;
            size_t soft_deleted = 0;
            size_t locked = 0;
            size_t metadata_available = 0;

            for (auto itr = ((lockfree_single_block_resource_with_stats *)(args->mem_resource))->begin(); itr != ((lockfree_single_block_resource_with_stats *)(args->mem_resource))->end(); ++itr)
            {
                auto alloc_info = *itr;

                if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::IN_USE)
                {
                    in_use++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::AVAILABLE)
                {
                    available++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::SOFT_DELETED)
                {
                    soft_deleted++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::LOCKED)
                {
                    locked++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::METADATA_AVAILABLE)
                {
                    metadata_available++;
                }
                else if (alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::FRONTIER_RECLAIM_IN_PROGRESS ||
                         alloc_info.state == lockfree_single_block_resource_with_stats::allocation_state::FRONTIER_RECLAIMED)
                {
                    // Transient states during frontier reclamation walk — expected under concurrency
                }
                else
                {
                    CHECK(false);
                }
            }

            usleep(200000);
        }

        return nullptr;
    }

    // ---- Interrupt robustness scaffolding ------------------------------------------------
    static minstd::pmr::memory_resource *s_intr_resource = nullptr;
    static volatile sig_atomic_t s_intr_signal_count = 0;
    static volatile sig_atomic_t s_intr_nested_count = 0;
    static thread_local volatile sig_atomic_t s_intr_pending_ops = 0;

    static inline bool process_pending_intr_work(minstd::pmr::memory_resource *resource)
    {
        sig_atomic_t pending = s_intr_pending_ops;
        if (pending <= 0)
        {
            return false;
        }

        s_intr_pending_ops = 0;
        if (pending > 32)
        {
            pending = 32;
        }

        for (sig_atomic_t i = 0; i < pending; ++i)
        {
            void *p = guarded_allocate(resource, 16);
            if (p != nullptr)
            {
                guarded_deallocate(resource, p, 16);
                __atomic_add_fetch(&s_intr_nested_count, 1, __ATOMIC_SEQ_CST);
            }
        }

        return true;
    }

    static inline void drain_pending_intr_work(minstd::pmr::memory_resource *resource)
    {
        while (process_pending_intr_work(resource))
        {
        }
    }

    static bool settle_frontier_to_initial(lockfree_single_block_resource_with_stats &resource, size_t initial_frontier)
    {
        const size_t metadata_count = resource.debug_metadata_count();
        const size_t max_attempts = (metadata_count < 1000) ? 50000 : (metadata_count * 64);
        static constexpr size_t probe_sizes[] = {16, 4096, 65536, 1048576, 4194304};

        for (size_t attempt = 0; attempt < max_attempts; ++attempt)
        {
            if (resource.debug_frontier_offset() == initial_frontier)
            {
                return true;
            }

            const size_t probe_size = probe_sizes[attempt % (sizeof(probe_sizes) / sizeof(probe_sizes[0]))];
            void *probe = guarded_allocate(&resource, probe_size);
            if (probe != nullptr)
            {
                guarded_deallocate(&resource, probe, probe_size);
            }

            if ((attempt & 0xFF) == 0)
            {
                auto itr = resource.begin();
                (void)itr;
            }

            if ((attempt & 0x3F) == 0)
            {
                sched_yield();
            }
        }

        return resource.debug_frontier_offset() == initial_frontier;
    }

    static void sigusr1_nested_alloc_handler(int)
    {
        if (s_intr_resource != nullptr)
        {
            __atomic_add_fetch(&s_intr_pending_ops, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&s_intr_signal_count, 1, __ATOMIC_SEQ_CST);
        }
    }

    struct intr_test_bomber_args
    {
        pthread_t target_thread;
        minstd::atomic<bool> *stop_flag;
    };

    static void *intr_test_bomber_fn(void *arg)
    {
        auto *a = static_cast<intr_test_bomber_args *>(arg);
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            pthread_kill(a->target_thread, SIGUSR1);
            usleep(50);
        }
        return nullptr;
    }

    // ---- Soak test thread types ----------------------------------------------------------

    struct soak_thread_args
    {
        lockfree_single_block_resource_with_stats *resource;
        minstd::atomic<bool> *stop_flag;
        minstd::atomic<int> *shared_phase;
        minstd::atomic<bool> *use_shared_phase;
        minstd::atomic<int> *drained_count;
        minstd::atomic<int> *drain_epoch;
        minstd::atomic<int> *drain_ack_count;
        uint64_t rng_seed;
        size_t id;
        minstd::atomic<size_t> allocations{0};
        minstd::atomic<size_t> deallocations{0};
        minstd::atomic<size_t> failed_allocations{0};
        minstd::atomic<size_t> current_live_count{0};
        minstd::atomic<size_t> heartbeat{0};
        minstd::atomic<uint64_t> last_progress_ns{0};
        minstd::atomic<int> last_phase_seen{0};
        minstd::atomic<size_t> last_live_seen{0};
    };

    static inline uint64_t monotonic_time_ns()
    {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }

    static void *soak_worker_thread(void *arg)
    {
        auto *args = static_cast<soak_thread_args *>(arg);
        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(args->rng_seed, args->rng_seed * 10));

        constexpr size_t MAX_LIVE = 4000;
        void *pointers[MAX_LIVE]{};
        size_t sizes[MAX_LIVE]{};

        size_t live_count = 0;
        size_t loop_counter = 0;
        bool drain_signaled = false;

        int local_phase = 0;
        int local_cycle_count = 0;
        time_t local_phase_start = time(NULL);
        time_t local_phase_duration = 10 + ((int64_t)(rng() % 11) - 5);
        int last_acked_drain_epoch = -1;
        int previous_phase = -1;
        size_t burst_target_live = 2500;
        bool burst_pressure_spike = false;

        while (!args->stop_flag->load(minstd::memory_order_acquire))
        {
            process_pending_intr_work(args->resource);

            int current_phase;
            if (args->use_shared_phase->load(minstd::memory_order_acquire))
            {
                current_phase = args->shared_phase->load(minstd::memory_order_acquire);
            }
            else
            {
                if (++loop_counter % 100000 == 0)
                {
                    time_t now = time(NULL);
                    if (now - local_phase_start >= local_phase_duration)
                    {
                        if (local_phase == 0)
                        {
                            local_phase = 1;
                        }
                        else if (local_phase == 1)
                        {
                            local_phase = 2;
                        }
                        else
                        {
                            local_phase = 0;
                            local_cycle_count++;
                        }
                        local_phase_start = now;
                        local_phase_duration = 10 + ((int64_t)(rng() % 11) - 5);

                        static const char *pnames[] = {"STEADY", "BURSTY", "RECOVERY"};
                        printf("  [Thread %zu] Independent -> %s (duration: %zd secs, live: %zu)\n",
                               args->id, pnames[local_phase], (ssize_t)local_phase_duration, live_count);
                        fflush(stdout);
                    }
                }
                current_phase = local_phase;
            }

            if (current_phase != previous_phase)
            {
                if (current_phase == 1)
                {
                    burst_target_live = 2200 + (rng() % 1701);
                    burst_pressure_spike = ((rng() % 5) == 0);
                    if (burst_pressure_spike)
                    {
                        burst_target_live = 3800 + (rng() % 151);
                    }
                }
                previous_phase = current_phase;
            }

            args->last_phase_seen.store(current_phase, minstd::memory_order_relaxed);
            args->last_live_seen.store(live_count, minstd::memory_order_relaxed);

            if (current_phase == 3)
            {
                const int drain_epoch = args->drain_epoch->load(minstd::memory_order_acquire);
                if (drain_epoch != last_acked_drain_epoch)
                {
                    args->drain_ack_count->fetch_add(1, minstd::memory_order_release);
                    last_acked_drain_epoch = drain_epoch;
                }
            }

            int alloc_pct;
            if (current_phase == 0)
            {
                alloc_pct = 50;
            }
            else if (current_phase == 1)
            {
                if (live_count < burst_target_live)
                {
                    alloc_pct = burst_pressure_spike ? 98 : 92;
                }
                else
                {
                    alloc_pct = 50;
                }
            }
            else if (current_phase == 2)
            {
                alloc_pct = (live_count > 10) ? 10 : 50;
            }
            else
            {
                alloc_pct = 0;
            }

            if (current_phase == 3)
            {
                if (live_count == 0)
                {
                    if (!drain_signaled)
                    {
                        args->drained_count->fetch_add(1, minstd::memory_order_release);
                        drain_signaled = true;
                    }
                    sched_yield();
                    continue;
                }
            }
            else
            {
                drain_signaled = false;
            }

            bool do_alloc;
            if (live_count == 0 && current_phase != 3)
            {
                do_alloc = true;
            }
            else if (live_count >= MAX_LIVE)
            {
                do_alloc = false;
            }
            else
            {
                do_alloc = ((int)(rng() % 100)) < alloc_pct;
            }

            if (do_alloc)
            {
                size_t sz = 1 + (rng() % 32000);
                void *p = guarded_allocate(args->resource, sz);
                if (p)
                {
                    pointers[live_count] = p;
                    sizes[live_count] = sz;
                    live_count++;
                    args->allocations.fetch_add(1, minstd::memory_order_relaxed);
                    args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                    args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
                }
                else
                {
                    args->failed_allocations.fetch_add(1, minstd::memory_order_relaxed);
                    args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                    args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
                }
            }
            else if (live_count > 0)
            {
                size_t idx = rng() % live_count;
                guarded_deallocate(args->resource, pointers[idx], sizes[idx]);
                pointers[idx] = pointers[live_count - 1];
                sizes[idx] = sizes[live_count - 1];
                live_count--;
                args->deallocations.fetch_add(1, minstd::memory_order_relaxed);
                args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
            }
            else
            {
                sched_yield();
            }

            args->current_live_count.store(live_count, minstd::memory_order_relaxed);

            const size_t ops = args->allocations.load(minstd::memory_order_relaxed) + args->deallocations.load(minstd::memory_order_relaxed);
            if (ops % 1000 == 0)
            {
                sched_yield();
            }
        }

        drain_pending_intr_work(args->resource);

        for (size_t i = 0; i < live_count; ++i)
        {
            guarded_deallocate(args->resource, pointers[i], sizes[i]);
            args->deallocations.fetch_add(1, minstd::memory_order_relaxed);
        }

        return nullptr;
    }

    struct soak_bomber_args
    {
        pthread_t *targets;
        size_t num_targets;
        minstd::atomic<bool> *stop_flag;
    };

    static void *soak_bomber_thread(void *arg)
    {
        auto *a = static_cast<soak_bomber_args *>(arg);
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            for (size_t i = 0; i < a->num_targets; i++)
            {
                pthread_kill(a->targets[i], SIGUSR1);
            }
            usleep(100);
        }
        return nullptr;
    }
    // -------------------------------------------------------------------------------------
}
