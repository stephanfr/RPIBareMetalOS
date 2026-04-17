// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../shared/lockfree_skiplist_test_helpers.h"
#include "../shared/soak_test_config.h"

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistSoakTests)
    {
    };
#pragma GCC diagnostic pop

    struct intr_multi_bomber_args
    {
        size_t num_targets;
        pthread_t targets[16];
        minstd::atomic<bool> *stop_flag;
    };

    static void *intr_multi_bomber_fn(void *arg)
    {
        auto *a = static_cast<intr_multi_bomber_args *>(arg);
        size_t target_idx = 0;
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            pthread_kill(a->targets[target_idx], SIGUSR1);
            target_idx = (target_idx + 1) % a->num_targets;
            usleep(50);
        }
        return nullptr;
    }

    struct skiplist_soak_peaky_worker_args
    {
        intr_test_list_t *list_;
        minstd::pmr::memory_resource *memory_resource_;
        minstd::atomic<bool> *start_;
        minstd::atomic<size_t> *ready_count_;
        minstd::atomic<size_t> *allocation_failures_;
        minstd::atomic<bool> *stop_;

        uint32_t thread_id_;
        uint32_t key_space_;

        size_t operations_completed_;
        size_t composite_allocations_;
        size_t insert_successes_;
        size_t remove_successes_;
        size_t find_successes_;
        size_t find_failures_;
    };

    static void *skiplist_soak_peaky_worker_fn(void *arg)
    {
        auto *args = static_cast<skiplist_soak_peaky_worker_args *>(arg);

        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(100, args->thread_id_ + 1000));

        args->ready_count_->fetch_add(1, minstd::memory_order_release);
        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        size_t ops_since_reconfig = 0;
        uint32_t write_period = 0;
        const uint32_t possible_write_periods[] = {5, 10, 50, 100, 1000, 0};

        while (!args->stop_->load(minstd::memory_order_relaxed))
        {
            if (ops_since_reconfig > 50000)
            {
                write_period = possible_write_periods[rng() % 6];
                ops_since_reconfig = 0;
            }

            uint32_t op_val = rng();

            const size_t block_size = 32 + static_cast<size_t>(op_val & 0xFFu);
            void *ptr = args->memory_resource_->allocate(block_size);

            if (ptr == nullptr)
            {
                args->allocation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }
            else
            {
                args->memory_resource_->deallocate(ptr, block_size);
                args->composite_allocations_++;
            }

            if (op_val % 10000 == 0)
            {
                size_t count = 0;
                for (const auto &kv : *args->list_)
                {
                    (void)kv;
                    count++;
                }
            }

            bool is_write = (write_period != 0) && ((op_val % write_period) == 0);
            uint32_t key = (op_val >> 16) % args->key_space_;

            if (is_write)
            {
                if (op_val & 1)
                {
                    if (args->list_->insert(key, key))
                    {
                        args->insert_successes_++;
                    }
                }
                else
                {
                    if (args->list_->remove(key))
                    {
                        args->remove_successes_++;
                    }
                }
            }
            else
            {
                if (args->list_->find(key) != args->list_->end())
                {
                    args->find_successes_++;
                }
                else
                {
                    args->find_failures_++;
                }
            }

            args->operations_completed_++;
            ops_since_reconfig++;
        }

        return nullptr;
    }

    TEST(SkiplistSoakTests, PeakyDynamicLoadSoakTest)
    {
        const size_t NUM_THREADS = 8;
        const uint32_t KEY_SPACE = 10000;

        const size_t SOAK_DURATION_SEC = soak_config_get_duration_sec("SKIPLIST_SOAK_DURATION", 1);

        struct sigaction sa;
        sa.sa_handler = sigusr1_nested_read_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, nullptr);

        s_intr_signal_count = 0;
        s_intr_nested_count = 0;

        intr_test_list_t list;
        s_intr_list = &list;

        for (uint32_t i = 0; i < KEY_SPACE / 2; ++i)
        {
            CHECK_TRUE(list.insert(i, i));
        }

        minstd::pmr::lockfree_composite_single_arena_resource<1000, 64, 1024, 32, 512, true> composite_resource(
            skiplist_composite_resource_buffer,
            SKIPLIST_COMPOSITE_BUFFER_SIZE,
            skiplist_perf_num_arenas());

        pthread_t workers[16]{};
        skiplist_soak_peaky_worker_args thread_args[16]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<bool> stop{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> allocation_failures{0};

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].memory_resource_ = &composite_resource;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].allocation_failures_ = &allocation_failures;
            thread_args[i].stop_ = &stop;
            thread_args[i].thread_id_ = i;
            thread_args[i].key_space_ = KEY_SPACE;
            thread_args[i].operations_completed_ = 0;
            thread_args[i].composite_allocations_ = 0;
            thread_args[i].insert_successes_ = 0;
            thread_args[i].remove_successes_ = 0;
            thread_args[i].find_successes_ = 0;
            thread_args[i].find_failures_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_soak_peaky_worker_fn, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < NUM_THREADS)
        {
            sched_yield();
        }

        pthread_t bomber_thread;
        intr_multi_bomber_args bomber_args;
        bomber_args.num_targets = NUM_THREADS;
        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            bomber_args.targets[i] = workers[i];
        }
        bomber_args.stop_flag = &stop;

        CHECK_EQUAL(0, pthread_create(&bomber_thread, nullptr, intr_multi_bomber_fn, &bomber_args));

        start.store(true, minstd::memory_order_release);

        printf("\nRunning PeakyDynamicLoadSoakTest for %zu seconds...\n", SOAK_DURATION_SEC);

        timespec start_time{};
        timespec end_time{};
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        size_t last_print_sec = 0;
        while (true)
        {
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            double elapsed = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                             (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);

            if (elapsed >= static_cast<double>(SOAK_DURATION_SEC))
            {
                break;
            }
            size_t current_sec = static_cast<size_t>(elapsed);
            if (current_sec - last_print_sec >= 10)
            {
                size_t current_ops = 0;
                for (size_t i = 0; i < NUM_THREADS; ++i)
                {
                    current_ops += thread_args[i].operations_completed_;
                }
                printf("  ... elapsed: %zu / %zu seconds (Ops so far: %zu)\n", current_sec, SOAK_DURATION_SEC, current_ops);
                fflush(stdout);
                last_print_sec = current_sec;
            }
            usleep(100000);
        }

        stop.store(true, minstd::memory_order_release);

        size_t total_operations = 0;
        size_t total_composite_allocations = 0;
        size_t total_inserts = 0;
        size_t total_removes = 0;

        for (size_t i = 0; i < NUM_THREADS; ++i)
        {
            pthread_join(workers[i], nullptr);
            total_operations += thread_args[i].operations_completed_;
            total_composite_allocations += thread_args[i].composite_allocations_;
            total_inserts += thread_args[i].insert_successes_;
            total_removes += thread_args[i].remove_successes_;
        }
        pthread_join(bomber_thread, nullptr);

        printf("Soak test completed. Operations: %zu, Mem Allocs: %zu, Inserts: %zu, Removes: %zu\n",
               total_operations, total_composite_allocations, total_inserts, total_removes);
        printf("Pool allocations: %zu, Deallocations: %zu, Current allocated bytes: %zu\n",
               composite_resource.total_allocations(), composite_resource.total_deallocations(),
               composite_resource.current_allocated());
        printf("Signals delivered: %u, Nested reads: %u\n",
               (uint32_t)s_intr_signal_count, (uint32_t)s_intr_nested_count);

        CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
        CHECK_TRUE(s_intr_signal_count > 0);

        CHECK_TRUE(list.validate_ordering());

        CHECK_EQUAL(static_cast<size_t>(0), composite_resource.current_allocated());
        CHECK_EQUAL(composite_resource.total_allocations(), composite_resource.total_deallocations());

        s_intr_list = nullptr;
    }
}
