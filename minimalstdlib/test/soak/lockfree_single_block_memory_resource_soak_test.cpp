// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../shared/lockfree_single_block_resource_test_helpers.h"
#include "../shared/soak_test_config.h"

// clang-format off

TEST_GROUP(LockfreeSingleBlockMemoryResourceSoakTests)
{
};

TEST(LockfreeSingleBlockMemoryResourceSoakTests, SoakTest)
{
    const size_t NUM_THREADS = 8;
    const bool ENABLE_SIGNAL_BOMBER = (getenv("ALLOCATOR_SOAK_DISABLE_BOMBER") == nullptr);
    size_t POST_DRAIN_BURST_OPS = 0;
    if (const char* post_drain_burst_ops_env = getenv("ALLOCATOR_SOAK_POST_DRAIN_BURST_OPS"))
    {
        POST_DRAIN_BURST_OPS = strtoull(post_drain_burst_ops_env, nullptr, 10);
    }
    size_t POST_DRAIN_BURST_SIZE = 4096;
    if (const char* post_drain_burst_size_env = getenv("ALLOCATOR_SOAK_POST_DRAIN_BURST_SIZE"))
    {
        POST_DRAIN_BURST_SIZE = strtoull(post_drain_burst_size_env, nullptr, 10);
    }

    const size_t SOAK_DURATION_SEC = soak_config_get_duration_sec("ALLOCATOR_SOAK_DURATION", 180);

    uint64_t base_seed = 987654321ULL;
    const char* soak_seed_env = getenv("ALLOCATOR_SOAK_SEED");
    if (soak_seed_env)
    {
        base_seed = strtoull(soak_seed_env, nullptr, 10);
    }
    else
    {
        base_seed += time(nullptr);
    }

    printf("\nRunning Allocator SoakTest for %zu seconds (Base Seed: %llu)...\n", SOAK_DURATION_SEC, (unsigned long long)base_seed);

    lockfree_single_block_resource_with_stats resource(buffer, BUFFER_SIZE);
    const size_t initial_frontier = resource.debug_frontier_offset();

    struct sigaction sa = {};
    sa.sa_handler = sigusr1_nested_alloc_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);

    s_intr_resource = &resource;
    s_intr_signal_count = 0;
    s_intr_nested_count = 0;
    s_intr_pending_ops = 0;

    minstd::atomic<bool> stop_flag{false};
    minstd::atomic<int> shared_phase{0};
    minstd::atomic<bool> use_shared_phase{true};
    minstd::atomic<int> drained_count{0};
    minstd::atomic<int> drain_epoch{0};
    minstd::atomic<int> drain_ack_count{0};

    pthread_t workers[NUM_THREADS]{};
    soak_thread_args thread_args[NUM_THREADS]{};

    for (size_t i = 0; i < NUM_THREADS; ++i)
    {
        thread_args[i].resource = &resource;
        thread_args[i].stop_flag = &stop_flag;
        thread_args[i].shared_phase = &shared_phase;
        thread_args[i].use_shared_phase = &use_shared_phase;
        thread_args[i].drained_count = &drained_count;
        thread_args[i].drain_epoch = &drain_epoch;
        thread_args[i].drain_ack_count = &drain_ack_count;
        thread_args[i].rng_seed = base_seed + i;
        thread_args[i].id = i;
        thread_args[i].last_progress_ns.store(monotonic_time_ns(), minstd::memory_order_relaxed);

        CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, soak_worker_thread, &thread_args[i]));
    }

    soak_bomber_args b_args;
    b_args.targets = workers;
    b_args.num_targets = NUM_THREADS;
    b_args.stop_flag = &stop_flag;
    b_args.shared_phase = &shared_phase;

    pthread_t bomber;
    if (ENABLE_SIGNAL_BOMBER)
    {
        CHECK_EQUAL(0, pthread_create(&bomber, nullptr, soak_bomber_thread, &b_args));
    }
    else
    {
        printf("Signal bomber disabled for this run.\n");
        fflush(stdout);
    }

    minstd::Xoroshiro128PlusPlusRNG main_rng(minstd::Xoroshiro128PlusPlusRNG::Seed(123456789ULL ^ base_seed, base_seed));
    int main_phase = 0;
    int main_cycle_count = 0;
    bool main_shared_mode = true;
    time_t phase_start = time(NULL);
    time_t phase_dur = 15 + ((int64_t)(main_rng() % 11) - 5);

    static const char* phase_names[] = {"STEADY", "BURSTY", "RECOVERY", "DRAIN"};
    printf("Phase -> %s [SHARED] (duration: %zd secs)\n", phase_names[main_phase], (ssize_t)phase_dur);
    printf("Post-drain single-thread burst: ops=%zu, size=%zu bytes\n", POST_DRAIN_BURST_OPS, POST_DRAIN_BURST_SIZE);
    fflush(stdout);

    size_t drain_meta_start = resource.debug_metadata_count();
    size_t drain_frontier_start = resource.debug_frontier_offset();
    size_t post_drain_burst_allocs = 0;
    size_t post_drain_burst_deallocs = 0;

    size_t elapsed = 0;
    size_t last_allocs = 0;
    size_t last_deallocs = 0;
    size_t last_failed = 0;
    size_t last_heartbeats[NUM_THREADS]{};
    size_t hb_deltas[NUM_THREADS]{};

    while (elapsed < SOAK_DURATION_SEC * 10)
    {
        if (elapsed % 10 == 0)
        {
            time_t now = time(NULL);
            bool transition = false;

            if (main_phase == 3)
            {
                const int acked = drain_ack_count.load(minstd::memory_order_acquire);
                const int drained = drained_count.load(minstd::memory_order_acquire);
                if (acked >= (int)NUM_THREADS && drained >= (int)NUM_THREADS)
                {
                    transition = true;
                }
            }
            else if (now - phase_start >= phase_dur)
            {
                transition = true;
            }

            if (transition)
            {
                if (main_phase == 0)
                {
                    main_phase = 1;
                }
                else if (main_phase == 1)
                {
                    main_phase = 2;
                }
                else if (main_phase == 2)
                {
                    main_cycle_count++;
                    if (main_cycle_count % 4 == 0)
                    {
                        main_phase = 3;
                    }
                    else
                    {
                        main_phase = 0;
                        main_shared_mode = (main_rng() % 2) == 0;
                    }
                }
                else
                {
                    const size_t meta_after_drain = resource.debug_metadata_count();
                    size_t frontier_after_drain = resource.debug_frontier_offset();
                    printf("  [DrainComplete] meta_before=%zu meta_after=%zu delta=%zd frontier_before=%zu frontier_after=%zu delta=%zd\n",
                           drain_meta_start,
                           meta_after_drain,
                           static_cast<ssize_t>(meta_after_drain) - static_cast<ssize_t>(drain_meta_start),
                           drain_frontier_start,
                           frontier_after_drain,
                           static_cast<ssize_t>(frontier_after_drain) - static_cast<ssize_t>(drain_frontier_start));
                    if (frontier_after_drain != initial_frontier)
                    {
                        printf("  [DrainComplete] frontier=%zu (initial=%zu); defer strict check to final quiescent validation\n",
                               frontier_after_drain, initial_frontier);
                    }
                    fflush(stdout);

                    main_phase = 0;
                    main_shared_mode = (main_rng() % 2) == 0;
                }

                phase_start = now;
                phase_dur = 15 + ((int64_t)(main_rng() % 11) - 5);

                if (main_phase == 3)
                {
                    use_shared_phase.store(true, minstd::memory_order_release);
                    drained_count.store(0, minstd::memory_order_release);
                    drain_ack_count.store(0, minstd::memory_order_release);
                    drain_epoch.fetch_add(1, minstd::memory_order_acq_rel);
                    drain_meta_start = resource.debug_metadata_count();
                    drain_frontier_start = resource.debug_frontier_offset();
                    printf("  [DrainStart] meta=%zu frontier=%zu\n", drain_meta_start, drain_frontier_start);
                    fflush(stdout);
                }
                else
                {
                    use_shared_phase.store(main_shared_mode, minstd::memory_order_release);
                }
                shared_phase.store(main_phase, minstd::memory_order_release);

                printf("Phase -> %s [%s] (duration: %zd secs, cycle: %d)\n",
                       phase_names[main_phase],
                       (main_phase == 3 || main_shared_mode) ? "SHARED" : "INDEPENDENT",
                       (ssize_t)phase_dur, main_cycle_count);
                fflush(stdout);
            }
        }

        if (elapsed % 100 == 0 && elapsed > 0)
        {
            size_t c_allocs = 0, c_deallocs = 0, c_failed = 0, c_live = 0;
            for (size_t i = 0; i < NUM_THREADS; ++i)
            {
                c_allocs += thread_args[i].allocations.load(minstd::memory_order_relaxed);
                c_deallocs += thread_args[i].deallocations.load(minstd::memory_order_relaxed);
                c_failed += thread_args[i].failed_allocations.load(minstd::memory_order_relaxed);
                c_live += thread_args[i].current_live_count.load(minstd::memory_order_relaxed);
            }

            for (size_t i = 0; i < NUM_THREADS; ++i)
            {
                const size_t hb = thread_args[i].heartbeat.load(minstd::memory_order_relaxed);
                hb_deltas[i] = hb - last_heartbeats[i];
                last_heartbeats[i] = hb;
            }

            size_t allocs_per_sec = (c_allocs - last_allocs) / 10;
            size_t deallocs_per_sec = (c_deallocs - last_deallocs) / 10;
            size_t failed_per_sec = (c_failed - last_failed) / 10;
            last_allocs = c_allocs;
            last_deallocs = c_deallocs;
            last_failed = c_failed;

            size_t frontier_off = resource.debug_frontier_offset();
            size_t meta_count = resource.debug_metadata_count();
            size_t meta_boundary = resource.debug_metadata_boundary_offset();
            size_t gap = (meta_boundary > frontier_off) ? (meta_boundary - frontier_off) : 0;

            printf("Elapsed: %zu secs, Live: %zu, Allocs: %zu ( %zu /sec ), Deallocs: %zu ( %zu /sec ), Failed: %zu ( %zu /sec )\n",
                   elapsed / 10, c_live, c_allocs, allocs_per_sec, c_deallocs, deallocs_per_sec, c_failed, failed_per_sec);
            printf("  Frontier: %zuMB, MetaCount: %zu, MetaBoundary: %zuMB, Gap: %zuMB\n",
                   frontier_off / (1024*1024), meta_count, meta_boundary / (1024*1024), gap / (1024*1024));
            fflush(stdout);

            if (main_phase == 3 && allocs_per_sec == 0 && deallocs_per_sec == 0)
            {
                int drained = drained_count.load(minstd::memory_order_acquire);
                int acked = drain_ack_count.load(minstd::memory_order_acquire);
                const uint64_t now_ns = monotonic_time_ns();
                printf("  [DRAIN STALL?] acked=%d/%zu drained_count=%d/%zu\n", acked, NUM_THREADS, drained, NUM_THREADS);
                  printf("  [ALLOC DBG] calls=%zu reuse=%zu frontier=%zu alloc_fail=%zu search_iter=%zu pops=%zu claimed=%zu reclaim_deferred=%zu frontier_cas_retry=%zu metadata_cas_retry=%zu\n",
                      resource.debug_alloc_calls(),
                      resource.debug_alloc_reuse_hits(),
                      resource.debug_alloc_frontier_hits(),
                      resource.debug_alloc_failures(),
                      resource.debug_search_iterations(),
                      resource.debug_search_pops(),
                      resource.debug_search_claimed(),
                      resource.debug_search_reclaim_deferred(),
                      resource.debug_frontier_cas_retries(),
                      resource.debug_metadata_cas_retries());
                for (size_t i = 0; i < NUM_THREADS; ++i)
                {
                    uint64_t last_ns = thread_args[i].last_progress_ns.load(minstd::memory_order_relaxed);
                    uint64_t age_ms = (now_ns > last_ns) ? ((now_ns - last_ns) / 1000000ULL) : 0ULL;
                    int t_phase = thread_args[i].last_phase_seen.load(minstd::memory_order_relaxed);
                    size_t t_live = thread_args[i].last_live_seen.load(minstd::memory_order_relaxed);
                                        uint32_t marker = thread_args[i].last_marker.load(minstd::memory_order_relaxed);
                                        size_t pending = thread_args[i].last_pending_seen.load(minstd::memory_order_relaxed);
                                        size_t loops = thread_args[i].loop_iterations.load(minstd::memory_order_relaxed);
                                        printf("    [T%zu] phase=%d live=%zu marker=%u pending=%zu loops=%zu hb_delta=%zu age_ms=%llu alloc=%zu dealloc=%zu failed_alloc=%zu failed_dealloc=%zu\n",
                                                     i, t_phase, t_live, marker, pending, loops, hb_deltas[i], (unsigned long long)age_ms,
                                                     thread_args[i].allocations.load(minstd::memory_order_relaxed),
                                                     thread_args[i].deallocations.load(minstd::memory_order_relaxed),
                                                     thread_args[i].failed_allocations.load(minstd::memory_order_relaxed),
                                                     thread_args[i].failed_deallocations.load(minstd::memory_order_relaxed));
                }
                fflush(stdout);

                if (getenv("ALLOCATOR_SOAK_BREAK_ON_STALL"))
                {
                    raise(SIGTRAP);
                }
            }
        }

        usleep(100000); // 100ms
        elapsed++;
    }

    // Force a final shared DRAIN phase so teardown checks run from a true quiescent point
    const size_t final_drain_meta_start = resource.debug_metadata_count();
    const size_t final_drain_frontier_start = resource.debug_frontier_offset();
    printf("[FinalDrainStart] meta=%zu frontier=%zu\n", final_drain_meta_start, final_drain_frontier_start);
    fflush(stdout);

    use_shared_phase.store(true, minstd::memory_order_release);
    drained_count.store(0, minstd::memory_order_release);
    drain_ack_count.store(0, minstd::memory_order_release);
    drain_epoch.fetch_add(1, minstd::memory_order_acq_rel);
    shared_phase.store(3, minstd::memory_order_release);

    const size_t FINAL_DRAIN_WAIT_TICKS = 300; // 30 seconds max
    bool final_drain_complete = false;
    for (size_t tick = 0; tick < FINAL_DRAIN_WAIT_TICKS; ++tick)
    {
        int acked = drain_ack_count.load(minstd::memory_order_acquire);
        int drained = drained_count.load(minstd::memory_order_acquire);
        if (acked >= (int)NUM_THREADS && drained >= (int)NUM_THREADS)
        {
            final_drain_complete = true;
            break;
        }
        usleep(100000);
    }

    if (!final_drain_complete)
    {
        printf("[FinalDrainTimeout] acked=%d/%zu drained=%d/%zu\n",
               drain_ack_count.load(minstd::memory_order_acquire), NUM_THREADS,
               drained_count.load(minstd::memory_order_acquire), NUM_THREADS);
        fflush(stdout);
    }
    else
    {
        const size_t final_drain_meta_after = resource.debug_metadata_count();
        const size_t final_drain_frontier_after = resource.debug_frontier_offset();
        printf("[FinalDrainComplete] meta_before=%zu meta_after=%zu delta=%zd frontier_before=%zu frontier_after=%zu delta=%zd\n",
               final_drain_meta_start,
               final_drain_meta_after,
               static_cast<ssize_t>(final_drain_meta_after) - static_cast<ssize_t>(final_drain_meta_start),
               final_drain_frontier_start,
               final_drain_frontier_after,
               static_cast<ssize_t>(final_drain_frontier_after) - static_cast<ssize_t>(final_drain_frontier_start));
        fflush(stdout);
    }

    if (final_drain_complete && POST_DRAIN_BURST_OPS > 0 && POST_DRAIN_BURST_SIZE > 0)
    {
        const size_t burst_meta_before = resource.debug_metadata_count();
        const size_t burst_frontier_before = resource.debug_frontier_offset();
        size_t burst_alloc_failures = 0;

        for (size_t i = 0; i < POST_DRAIN_BURST_OPS; ++i)
        {
            void *p = guarded_allocate(&resource, POST_DRAIN_BURST_SIZE);
            if (p == nullptr)
            {
                burst_alloc_failures++;
            }
            else
            {
                post_drain_burst_allocs++;
                guarded_deallocate(&resource, p, POST_DRAIN_BURST_SIZE);
                post_drain_burst_deallocs++;
            }

            if ((i & 0x3FF) == 0)
            {
                sched_yield();
            }
        }

        const size_t burst_meta_after = resource.debug_metadata_count();
        const size_t burst_frontier_after = resource.debug_frontier_offset();
        printf("[PostDrainBurst] ops=%zu size=%zu allocs=%zu deallocs=%zu alloc_failures=%zu meta_before=%zu meta_after=%zu delta=%zd frontier_before=%zu frontier_after=%zu delta=%zd\n",
               POST_DRAIN_BURST_OPS,
               POST_DRAIN_BURST_SIZE,
               post_drain_burst_allocs,
               post_drain_burst_deallocs,
               burst_alloc_failures,
               burst_meta_before,
               burst_meta_after,
               static_cast<ssize_t>(burst_meta_after) - static_cast<ssize_t>(burst_meta_before),
               burst_frontier_before,
               burst_frontier_after,
               static_cast<ssize_t>(burst_frontier_after) - static_cast<ssize_t>(burst_frontier_before));
        fflush(stdout);
    }

    stop_flag.store(true, minstd::memory_order_release);

    if (ENABLE_SIGNAL_BOMBER)
    {
        pthread_join(bomber, nullptr);
    }

    for (size_t i = 0; i < NUM_THREADS; ++i)
    {
        pthread_join(workers[i], nullptr);
    }

    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, nullptr);
    s_intr_resource = nullptr;

    size_t total_alloc = 0;
    size_t total_dealloc = 0;
    size_t total_failed = 0;
    for (size_t i = 0; i < NUM_THREADS; ++i)
    {
        total_alloc += thread_args[i].allocations.load(minstd::memory_order_relaxed);
        total_dealloc += thread_args[i].deallocations.load(minstd::memory_order_relaxed);
        total_failed += thread_args[i].failed_allocations.load(minstd::memory_order_relaxed);
    }

    // Validate worker + interrupt accounting before frontier-settle probes,
    // because settle_frontier_to_initial performs probe alloc/dealloc pairs.
    const size_t expected_alloc_before_settle = total_alloc + s_intr_nested_count + post_drain_burst_allocs;
    const size_t expected_dealloc_before_settle = total_dealloc + s_intr_nested_count + post_drain_burst_deallocs;
    const size_t observed_aborted_dealloc_before_settle = resource.aborted_deallocations();
    const size_t aborted_bad_index = resource.aborted_deallocations_bad_index();
    const size_t aborted_hash_mismatch = resource.aborted_deallocations_hash_mismatch();
    const size_t aborted_state_mismatch = resource.aborted_deallocations_state_mismatch();
    const size_t aborted_cas_race = resource.aborted_deallocations_cas_race();
    CHECK_EQUAL(expected_alloc_before_settle, resource.total_allocations());
    CHECK_EQUAL(expected_dealloc_before_settle,
                resource.total_deallocations() + observed_aborted_dealloc_before_settle);
    CHECK_EQUAL(observed_aborted_dealloc_before_settle,
                aborted_bad_index + aborted_hash_mismatch + aborted_state_mismatch + aborted_cas_race);

    bool frontier_settled = settle_frontier_to_initial(resource, initial_frontier);
    if (!frontier_settled)
    {
        printf("[QuiescentSettle] frontier remained at %zu (initial=%zu, metadata=%zu)\n",
               resource.debug_frontier_offset(), initial_frontier, resource.debug_metadata_count());
        fflush(stdout);
    }

    printf("Soak test completed. Worker Allocs: %zu, Worker Deallocs: %zu, Failed Allocs: %zu (total across threads)\n",
           total_alloc, total_dealloc, total_failed);
        printf("Signals delivered: %d, Nested allocs triggered: %d\n", (int)s_intr_signal_count, (int)s_intr_nested_count);
        printf("Aborted deallocations observed: %zu\n", observed_aborted_dealloc_before_settle);
        printf("Aborted dealloc reasons: bad_index=%zu, hash_mismatch=%zu, state_mismatch=%zu, cas_race=%zu\n",
            aborted_bad_index, aborted_hash_mismatch, aborted_state_mismatch, aborted_cas_race);
    printf("Resource: total_allocs=%zu, total_deallocs=%zu, current_bytes=%zu, current_allocated=%zu\n",
           resource.total_allocations(), resource.total_deallocations(),
           resource.current_bytes_allocated(), resource.current_allocated());
    printf("Frontier offset: %zu, Metadata count: %zu\n",
           resource.debug_frontier_offset(), resource.debug_metadata_count());
    fflush(stdout);

    if (resource.current_bytes_allocated() != 0)
    {
        printf("*** LEAK DETECTED: current_bytes_allocated = %zu ***\n", resource.current_bytes_allocated());
        fflush(stdout);
    }
    CHECK_EQUAL(0, resource.current_bytes_allocated());

    const bool enforce_frontier_reset = (getenv("ALLOCATOR_SOAK_ENFORCE_FRONTIER_RESET") != nullptr);
    if (enforce_frontier_reset)
    {
        CHECK_TRUE(frontier_settled);
        CHECK_EQUAL(initial_frontier, resource.debug_frontier_offset());
    }
    else
    {
        CHECK_TRUE(resource.debug_frontier_offset() <= resource.debug_metadata_boundary_offset());
    }

    // Frontier settling may add balanced probe alloc/dealloc operations.
    CHECK_EQUAL(resource.total_allocations(), resource.total_deallocations());
}
