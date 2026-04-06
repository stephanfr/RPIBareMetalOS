// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/lockfree_single_block_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>

#include "interrupt_simulation_test_helpers.h"
#include "lockfree_single_block_resource_test_helpers.h"

#include <array>
#include <pthread.h>
#include <random>
#include <time.h>

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

namespace
{

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
        minstd::atomic<size_t> failed_deallocations{0};
        minstd::atomic<size_t> failed_allocations{0};
        minstd::atomic<size_t> current_live_count{0};
        minstd::atomic<size_t> heartbeat{0};
        minstd::atomic<uint64_t> last_progress_ns{0};
        minstd::atomic<int> last_phase_seen{0};
        minstd::atomic<size_t> last_live_seen{0};
        minstd::atomic<uint32_t> last_marker{0};
        minstd::atomic<size_t> last_pending_seen{0};
        minstd::atomic<size_t> loop_iterations{0};
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
            args->last_marker.store(MARK_LOOP_TOP, minstd::memory_order_relaxed);
            args->loop_iterations.fetch_add(1, minstd::memory_order_relaxed);
            args->last_pending_seen.store(static_cast<size_t>(s_intr_pending_ops), minstd::memory_order_relaxed);

            args->last_marker.store(MARK_PENDING_START, minstd::memory_order_relaxed);
            process_pending_intr_work(args->resource);
            args->last_marker.store(MARK_PENDING_DONE, minstd::memory_order_relaxed);

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

            args->last_marker.store(MARK_PHASE_READY, minstd::memory_order_relaxed);

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
                    args->last_marker.store(MARK_DRAIN_IDLE, minstd::memory_order_relaxed);
                    // spin wait
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
                args->last_marker.store(MARK_ALLOC_CALL, minstd::memory_order_relaxed);
                void *p = guarded_allocate(args->resource, sz);
                if (p)
                {
                    pointers[live_count] = p;
                    sizes[live_count] = sz;
                    live_count++;
                    args->last_marker.store(MARK_ALLOC_OK, minstd::memory_order_relaxed);
                    args->allocations.fetch_add(1, minstd::memory_order_relaxed);
                    args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                    args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
                }
                else
                {
                    args->last_marker.store(MARK_ALLOC_FAIL, minstd::memory_order_relaxed);
                    args->failed_allocations.fetch_add(1, minstd::memory_order_relaxed);
                    args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                    args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
                }
            }
            else if (live_count > 0)
            {
                size_t idx = rng() % live_count;
                void *ptr = pointers[idx];
                size_t sz = sizes[idx];
                args->last_marker.store(MARK_DEALLOC_CALL, minstd::memory_order_relaxed);
                guarded_deallocate(args->resource, ptr, sz);
                pointers[idx] = pointers[live_count - 1];
                sizes[idx] = sizes[live_count - 1];
                live_count--;
                args->last_marker.store(MARK_DEALLOC_DONE, minstd::memory_order_relaxed);
                args->deallocations.fetch_add(1, minstd::memory_order_relaxed);

                args->heartbeat.fetch_add(1, minstd::memory_order_relaxed);
                args->last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);
            }
            else
            {
                args->last_marker.store(MARK_EMPTY_YIELD, minstd::memory_order_relaxed);
                // spin wait
            }

            args->current_live_count.store(live_count, minstd::memory_order_relaxed);

            const size_t ops = args->allocations.load(minstd::memory_order_relaxed) + args->deallocations.load(minstd::memory_order_relaxed);
            if (ops % 1000 == 0)
            {
                // spin wait
            }
        }

        args->last_marker.store(MARK_SHUTDOWN_PENDING, minstd::memory_order_relaxed);
        drain_pending_intr_work(args->resource);

        for (size_t i = 0; i < live_count; ++i)
        {
            args->last_marker.store(MARK_SHUTDOWN_DEALLOC_CALL, minstd::memory_order_relaxed);
            guarded_deallocate(args->resource, pointers[i], sizes[i]);
            args->deallocations.fetch_add(1, minstd::memory_order_relaxed);
            args->last_marker.store(MARK_SHUTDOWN_DEALLOC_DONE, minstd::memory_order_relaxed);
        }

        args->last_marker.store(MARK_THREAD_EXIT, minstd::memory_order_relaxed);

        return nullptr;
    }

    struct soak_bomber_args
    {
        pthread_t *targets;
        size_t num_targets;
        minstd::atomic<bool> *stop_flag;
        minstd::atomic<int> *shared_phase;
    };

    static void *soak_bomber_thread(void *arg)
    {
        auto *a = static_cast<soak_bomber_args *>(arg);
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            // Keep stress during active phases, but avoid injecting signals while DRAIN is converging.
            const int phase = a->shared_phase->load(minstd::memory_order_acquire);
            if (phase != 3)
            {
                for (size_t i = 0; i < a->num_targets; i++)
                {
                    pthread_kill(a->targets[i], SIGUSR1);
                }
            }
            usleep(100);
        }
        return nullptr;
    }
    // -------------------------------------------------------------------------------------
}
