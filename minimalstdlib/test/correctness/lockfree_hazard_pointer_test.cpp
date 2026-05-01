// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/hazard_pointer>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (HazardPointerTests)
    {
    };
#pragma GCC diagnostic pop

    struct counted_node
    {
        uint32_t value_ = 0;
    };

    minstd::atomic<size_t> deleted_nodes{0};

    void counted_node_deleter(counted_node *ptr)
    {
        deleted_nodes.fetch_add(1, minstd::memory_order_relaxed);
        delete ptr;
    }

    void counted_node_free_deleter(counted_node *ptr)
    {
        deleted_nodes.fetch_add(1, minstd::memory_order_relaxed);
        free(ptr);
    }

    TEST(HazardPointerTests, RetireWithoutHazardReclaimsOnScan)
    {
        deleted_nodes.store(0, minstd::memory_order_relaxed);

        minstd::lockfree::hazard_pointer_domain<counted_node, 8, 32, 2> domain;

        auto *node = new counted_node{123};

        CHECK_TRUE(domain.retire(node, counted_node_deleter));
        CHECK_EQUAL(0u, deleted_nodes.load(minstd::memory_order_relaxed));

        CHECK_EQUAL(1u, domain.scan());
        CHECK_EQUAL(1u, deleted_nodes.load(minstd::memory_order_relaxed));
    }

    TEST(HazardPointerTests, HazardGuardDefersReclamationUntilCleared)
    {
        deleted_nodes.store(0, minstd::memory_order_relaxed);

        minstd::lockfree::hazard_pointer_domain<counted_node, 8, 32, 2> domain;
        minstd::atomic<counted_node *> shared{nullptr};

        auto *node = new counted_node{456};
        shared.store(node, minstd::memory_order_release);

        auto guard = domain.acquire_guard();
        CHECK_TRUE(guard.is_valid());

        auto *protected_ptr = guard.protect(shared);
        POINTERS_EQUAL(node, protected_ptr);
        CHECK_EQUAL(1u, domain.active_hazard_count());

        CHECK_TRUE(domain.retire(node, counted_node_deleter));
        CHECK_EQUAL(0u, domain.scan());
        CHECK_EQUAL(0u, deleted_nodes.load(minstd::memory_order_relaxed));

        guard.clear();
        CHECK_EQUAL(0u, domain.active_hazard_count());

        CHECK_EQUAL(1u, domain.scan());
        CHECK_EQUAL(1u, deleted_nodes.load(minstd::memory_order_relaxed));

        shared.store(nullptr, minstd::memory_order_release);
    }

    namespace
    {
        using test_domain_type = minstd::lockfree::hazard_pointer_domain<counted_node, 8, 64, 4>;
        using stress_domain_type = minstd::lockfree::hazard_pointer_domain<counted_node, 64, 4096, 32>;

        static constexpr size_t HAZARD_STRESS_MAX_THREADS = 16;
        static constexpr size_t HAZARD_STRESS_NUM_THREADS = 8;
        static constexpr size_t HAZARD_STRESS_ITERATIONS_PER_THREAD = 1000;

        struct hold_hazard_thread_args
        {
            test_domain_type *domain_ = nullptr;
            minstd::atomic<counted_node *> *shared_ = nullptr;
            minstd::atomic<bool> *entered_ = nullptr;
            minstd::atomic<bool> *release_ = nullptr;
        };

        struct hazard_stress_thread_args
        {
            stress_domain_type *domain_ = nullptr;
            minstd::atomic<counted_node *> *shared_slots_ = nullptr;
            minstd::atomic<bool> *start_ = nullptr;
            minstd::atomic<size_t> *ready_count_ = nullptr;
            uint32_t thread_id_ = 0;
            size_t iterations_ = 0;
            size_t operations_completed_ = 0;
        };

        void *hold_hazard_worker(void *arg)
        {
            auto *args = static_cast<hold_hazard_thread_args *>(arg);

            auto guard = args->domain_->acquire_guard();
            CHECK_TRUE(guard.is_valid());

            guard.protect(*args->shared_);
            args->entered_->store(true, minstd::memory_order_release);

            while (!args->release_->load(minstd::memory_order_acquire))
            {
                sched_yield();
            }

            return nullptr;
        }

        void *hazard_stress_worker(void *arg)
        {
            auto *args = static_cast<hazard_stress_thread_args *>(arg);

            auto guard = args->domain_->acquire_guard();
            CHECK_TRUE(guard.is_valid());

            args->ready_count_->fetch_add(1, minstd::memory_order_release);

            while (!args->start_->load(minstd::memory_order_acquire))
            {
                sched_yield();
            }

            auto &shared_slot = args->shared_slots_[args->thread_id_];

            for (size_t i = 0; i < args->iterations_; ++i)
            {
                auto *node = static_cast<counted_node *>(malloc(sizeof(counted_node)));
                CHECK_TRUE(node != nullptr);
                node->value_ = static_cast<uint32_t>((args->thread_id_ << 20) ^ i);
                shared_slot.store(node, minstd::memory_order_release);

                auto *protected_ptr = guard.protect(shared_slot);
                CHECK_TRUE(protected_ptr != nullptr);

                CHECK_TRUE(args->domain_->retire(protected_ptr, counted_node_free_deleter));

                guard.clear();
                shared_slot.store(nullptr, minstd::memory_order_release);
                args->domain_->scan();

                args->operations_completed_++;
            }

            return nullptr;
        }
    }

    TEST(HazardPointerTests, HazardBlocksReclaimAcrossThreads)
    {
        deleted_nodes.store(0, minstd::memory_order_relaxed);

        test_domain_type domain;
        minstd::atomic<counted_node *> shared{nullptr};

        auto *node = new counted_node{789};
        shared.store(node, minstd::memory_order_release);

        minstd::atomic<bool> entered{false};
        minstd::atomic<bool> release{false};

        hold_hazard_thread_args args;
        args.domain_ = &domain;
        args.shared_ = &shared;
        args.entered_ = &entered;
        args.release_ = &release;

        pthread_t worker{};
        CHECK_EQUAL(0, pthread_create(&worker, nullptr, hold_hazard_worker, &args));

        while (!entered.load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        CHECK_TRUE(domain.retire(node, counted_node_deleter));
        CHECK_EQUAL(0u, domain.scan());
        CHECK_EQUAL(0u, deleted_nodes.load(minstd::memory_order_relaxed));

        release.store(true, minstd::memory_order_release);
        CHECK_EQUAL(0, pthread_join(worker, nullptr));

        CHECK_EQUAL(1u, domain.scan());
        CHECK_EQUAL(1u, deleted_nodes.load(minstd::memory_order_relaxed));

        shared.store(nullptr, minstd::memory_order_release);
    }

    TEST(HazardPointerTests, MultiThreadedStressRetireProtectScan)
    {
        deleted_nodes.store(0, minstd::memory_order_relaxed);

        stress_domain_type domain;

        pthread_t workers[HAZARD_STRESS_NUM_THREADS]{};
        hazard_stress_thread_args thread_args[HAZARD_STRESS_NUM_THREADS]{};
        minstd::atomic<counted_node *> shared_slots[HAZARD_STRESS_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};

        for (size_t i = 0; i < HAZARD_STRESS_NUM_THREADS; ++i)
        {
            shared_slots[i].store(nullptr, minstd::memory_order_relaxed);

            thread_args[i].domain_ = &domain;
            thread_args[i].shared_slots_ = shared_slots;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = HAZARD_STRESS_ITERATIONS_PER_THREAD;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, hazard_stress_worker, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < HAZARD_STRESS_NUM_THREADS)
        {
            sched_yield();
        }

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < HAZARD_STRESS_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        size_t reclaimed_total = 0;

        for (size_t i = 0; i < 32; ++i)
        {
            const size_t reclaimed = domain.scan();
            reclaimed_total += reclaimed;

            if (reclaimed == 0)
            {
                break;
            }
        }

        const size_t expected_operations = HAZARD_STRESS_NUM_THREADS * HAZARD_STRESS_ITERATIONS_PER_THREAD;

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_TRUE(reclaimed_total <= expected_operations);
        CHECK_EQUAL(expected_operations, deleted_nodes.load(minstd::memory_order_relaxed));
    }

    TEST(HazardPointerTests, MultiThreadedStressThreadScaling)
    {
        printf("Hazard pointer protect/retire/scan ops/sec:\n");

        for (size_t num_threads = 1; num_threads <= HAZARD_STRESS_MAX_THREADS; ++num_threads)
        {
            deleted_nodes.store(0, minstd::memory_order_relaxed);

            stress_domain_type domain;

            pthread_t workers[HAZARD_STRESS_MAX_THREADS]{};
            hazard_stress_thread_args thread_args[HAZARD_STRESS_MAX_THREADS]{};
            minstd::atomic<counted_node *> shared_slots[HAZARD_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};

            for (size_t i = 0; i < num_threads; ++i)
            {
                shared_slots[i].store(nullptr, minstd::memory_order_relaxed);

                thread_args[i].domain_ = &domain;
                thread_args[i].shared_slots_ = shared_slots;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].iterations_ = HAZARD_STRESS_ITERATIONS_PER_THREAD;
                thread_args[i].operations_completed_ = 0;

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, hazard_stress_worker, &thread_args[i]));
            }

            while (ready_count.load(minstd::memory_order_acquire) < num_threads)
            {
                sched_yield();
            }

            timespec start_time{};
            timespec end_time{};
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            start.store(true, minstd::memory_order_release);

            size_t total_operations = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            for (size_t i = 0; i < 64; ++i)
            {
                if (domain.scan() == 0)
                {
                    break;
                }
            }

            const size_t expected_operations = num_threads * HAZARD_STRESS_ITERATIONS_PER_THREAD;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  %2zu threads: %f\n", num_threads, ops_per_sec);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(expected_operations, deleted_nodes.load(minstd::memory_order_relaxed));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
        }
    }
}
