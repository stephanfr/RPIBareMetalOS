// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../shared/lockfree_skiplist_test_helpers.h"

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistTests)
    {
    };
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistWriteCorrectnessTests)
    {
    };
#pragma GCC diagnostic pop

    TEST(SkiplistTests, BlockGarbageCollection)
    {
        // 1024 slots per block, so shift is 10.
        minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS, 16, 10, minstd::skiplist_extensions::skiplist_statistics> list;

        list.reset_slot_high_water_mark();

        for (uint32_t cycle = 0; cycle < 10; ++cycle)
        {
            for (uint32_t i = 1; i <= 15000; ++i)
            {
                CHECK_TRUE(list.insert(i, i));
            }

            CHECK_EQUAL(15000u, list.size());

            CHECK_TRUE(list.active_blocks() >= 14);

            for (uint32_t i = 1; i <= 14500; ++i)
            {
                CHECK_TRUE(list.remove(i));
            }

            CHECK_EQUAL(500u, list.size());

            for (uint32_t i = 0; i < 50; ++i)
            {
                list.try_advance_epoch(0);
                list.find(0xFFFFFFFF);
            }

            CHECK_TRUE(list.active_blocks() <= 5);

            for (uint32_t i = 14501; i <= 15000; ++i)
            {
                CHECK_TRUE(list.remove(i));
            }

            for (uint32_t i = 0; i < 50; ++i)
            {
                list.find(0xFFFFFFFF);
            }
        }
    }

    TEST(SkiplistTests, BasicFunctionality)
    {
        minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS> list;

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK_TRUE(list.insert(i, i));
        }

        CHECK_EQUAL(100u, list.size());

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK_TRUE(list.find(i) != list.end());
            CHECK_EQUAL(i, list.find(i)->second);
        }

        CHECK_TRUE(list.find(101) == list.end());

        for (uint32_t i = 0; i < 10; i++)
        {
            CHECK_TRUE(list.remove(i * 10));
            CHECK_TRUE(list.remove((i * 10) + 1));
        }

        CHECK_EQUAL(80u, list.size());

        for (uint32_t i = 0; i < 100; i++)
        {
            if ((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0)))
            {
                CHECK_TRUE(list.find(i) == list.end());
            }
            else
            {
                CHECK_TRUE(list.find(i) != list.end());
                CHECK_EQUAL(i, list.find(i)->second);
            }
        }

        CHECK_FALSE(list.insert(14, 114));
        CHECK_EQUAL(14u, list.find(14)->second);

        CHECK_FALSE(list.insert(37, 137));
        CHECK_EQUAL(37u, list.find(37)->second);

        CHECK_TRUE(list.insert(100, 100));

        CHECK_EQUAL(81u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == list.end());
            }
            else
            {
                CHECK_TRUE(list.find(i) != list.end());
                CHECK_EQUAL(i, list.find(i)->second);
            }
        }

        CHECK_EQUAL(81u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == list.end());
            }
            else
            {
                CHECK_TRUE(list.find(i) != list.end());
                CHECK_EQUAL(i, list.find(i)->second);
            }
        }

        CHECK_TRUE(list.insert(10, 10));
        CHECK_TRUE(list.insert(31, 31));
        CHECK_TRUE(list.insert(71, 71));

        CHECK_EQUAL(84u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) &&
                (i != 10) && (i != 31) && (i != 71) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == list.end());
            }
            else
            {
                CHECK_TRUE(list.find(i) != list.end());
                CHECK_EQUAL(i, list.find(i)->second);
            }
        }
    }

    TEST(SkiplistTests, ConstructorUsesProvidedMemoryResource)
    {
        class counting_memory_resource : public minstd::pmr::memory_resource
        {
        public:
            explicit counting_memory_resource(minstd::pmr::memory_resource *upstream)
                : upstream_(upstream)
            {
            }

            size_t allocations() const
            {
                return allocations_;
            }

            size_t deallocations() const
            {
                return deallocations_;
            }

        private:
            void *do_allocate(size_t bytes, size_t alignment) override
            {
                ++allocations_;
                return upstream_->allocate(bytes, alignment);
            }

            void do_deallocate(void *ptr, size_t bytes, size_t alignment) override
            {
                ++deallocations_;
                upstream_->deallocate(ptr, bytes, alignment);
            }

            bool do_is_equal(const minstd::pmr::memory_resource &other) const noexcept override
            {
                return this == &other;
            }

            minstd::pmr::memory_resource *upstream_;
            size_t allocations_ = 0;
            size_t deallocations_ = 0;
        };

        static unsigned char test_resource_buffer[2 * 1024 * 1024];
        minstd::pmr::lockfree_composite_single_arena_resource<> upstream_resource(test_resource_buffer, sizeof(test_resource_buffer), 2);
        counting_memory_resource counting_resource(&upstream_resource);

        const size_t allocations_before = counting_resource.allocations();
        const size_t deallocations_before = counting_resource.deallocations();

        {
            using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

            list_type list(&counting_resource);

            for (uint32_t i = 0; i < 64; ++i)
            {
                CHECK_TRUE(list.insert(i, i + 1));
            }

            for (uint32_t i = 0; i < 64; ++i)
            {
                auto item = list.find(i);
                CHECK_TRUE(item != list.end());
                CHECK_EQUAL(i + 1, item->second);
            }

            for (uint32_t i = 0; i < 32; ++i)
            {
                CHECK_TRUE(list.remove(i));
            }

            CHECK_TRUE(counting_resource.allocations() > allocations_before);
        }

        CHECK_TRUE(counting_resource.deallocations() > deallocations_before);
    }

    TEST(SkiplistTests, DefaultConstructorFallbackMatchesResourceConstructorBehavior)
    {
        //  The two lists must NOT coexist: atomic_forward_link::block_memory_resource_
        //  is a static shared across all instances of the same template instantiation.
        //  Run them in non-overlapping scopes and record results for cross-comparison.

        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        bool   default_found[128]{};
        uint32_t default_values[128]{};
        uint32_t default_size = 0;

        {
            list_type default_list;

            for (uint32_t i = 0; i < 128; ++i)
            {
                CHECK_TRUE(default_list.insert(i, i * 3));
            }

            for (uint32_t i = 0; i < 128; ++i)
            {
                auto item = default_list.find(i);
                CHECK_TRUE(item != default_list.end());
                CHECK_EQUAL(i * 3, item->second);
            }

            for (uint32_t i = 0; i < 64; ++i)
            {
                CHECK_TRUE(default_list.remove(i * 2));
            }

            default_size = default_list.size();

            for (uint32_t i = 0; i < 128; ++i)
            {
                auto item = default_list.find(i);
                default_found[i] = (item != default_list.end());
                if (default_found[i])
                {
                    default_values[i] = item->second;
                }
            }

            CHECK_TRUE(default_list.validate_ordering());
        }

        {
            static unsigned char resource_buffer[2 * 1024 * 1024];
            minstd::pmr::lockfree_composite_single_arena_resource<> resource(resource_buffer, sizeof(resource_buffer), 2);
            list_type resource_list(&resource);

            for (uint32_t i = 0; i < 128; ++i)
            {
                CHECK_TRUE(resource_list.insert(i, i * 3));
            }

            for (uint32_t i = 0; i < 128; ++i)
            {
                auto item = resource_list.find(i);
                CHECK_TRUE(item != resource_list.end());
                CHECK_EQUAL(i * 3, item->second);
            }

            for (uint32_t i = 0; i < 64; ++i)
            {
                CHECK_TRUE(resource_list.remove(i * 2));
            }

            CHECK_EQUAL(default_size, resource_list.size());

            for (uint32_t i = 0; i < 128; ++i)
            {
                auto resource_item = resource_list.find(i);
                const bool in_resource = (resource_item != resource_list.end());

                CHECK_EQUAL(default_found[i], in_resource);

                if (default_found[i] && in_resource)
                {
                    CHECK_EQUAL(default_values[i], resource_item->second);
                }
            }

            CHECK_TRUE(resource_list.validate_ordering());
        }
    }

    TEST(SkiplistTests, TemplateInstantiationWithCustomMaxLevels)
    {
        minstd::skip_list<uint32_t, uint64_t, SKIPLIST_STRESS_MAX_THREADS, 8> list;

        for (uint32_t i = 0; i < 256; ++i)
        {
            CHECK_TRUE(list.insert(i, static_cast<uint64_t>(i) * 10ull));
        }

        CHECK_EQUAL(256u, list.size());

        for (uint32_t i = 0; i < 256; ++i)
        {
            auto value = list.find(i);
            CHECK_TRUE(value != list.end());
            CHECK_EQUAL(static_cast<uint64_t>(i) * 10ull, value->second);
        }

        for (uint32_t i = 0; i < 128; ++i)
        {
            CHECK_TRUE(list.remove(i));
        }

        for (uint32_t i = 0; i < 128; ++i)
        {
            CHECK_TRUE(list.find(i) == list.end());
        }

        for (uint32_t i = 128; i < 256; ++i)
        {
            auto value = list.find(i);
            CHECK_TRUE(value != list.end());
            CHECK_EQUAL(static_cast<uint64_t>(i) * 10ull, value->second);
        }
    }

    TEST(SkiplistTests, MultiThreadedStressInsertFindRemove)
    {
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        list_type list;

        for (uint32_t i = 0; i < SKIPLIST_STRESS_KEY_SPACE; ++i)
        {
            CHECK_TRUE(list.insert(i, i));
        }

        pthread_t workers[SKIPLIST_STRESS_NUM_THREADS]{};
        skiplist_stress_thread_args<list_type> thread_args[SKIPLIST_STRESS_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> validation_failures{0};

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].validation_failures_ = &validation_failures;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = SKIPLIST_STRESS_ITERATIONS_PER_THREAD;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_stress_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < SKIPLIST_STRESS_NUM_THREADS)
        {
            sched_yield();
        }

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        const size_t expected_operations = SKIPLIST_STRESS_NUM_THREADS * SKIPLIST_STRESS_ITERATIONS_PER_THREAD;

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));

        CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());
    }

    TEST(SkiplistTests, MultiThreadedStressThreadScaling)
    {
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;
        // Correctness sweep across 1..MAX thread counts. Use the standard correctness
        // iteration budget rather than the perf-scaled multiplier (which is reserved
        // for the performance suite). The MultiThreadedStressInsertFindRemove and
        // MultiThreadedStressOrderingAndContentCorrectness siblings use the same
        // SKIPLIST_STRESS_ITERATIONS_PER_THREAD baseline.
        const size_t iterations_per_thread = SKIPLIST_STRESS_ITERATIONS_PER_THREAD;

        for (size_t num_threads = 1; num_threads <= SKIPLIST_STRESS_MAX_THREADS; ++num_threads)
        {
            list_type list;

            for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
            {
                CHECK_TRUE(list.insert(key, key));
            }

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            skiplist_stress_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};
            minstd::atomic<size_t> validation_failures{0};

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].list_ = &list;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].validation_failures_ = &validation_failures;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_stress_worker<list_type>, &thread_args[i]));
            }

            while (ready_count.load(minstd::memory_order_acquire) < num_threads)
            {
                sched_yield();
            }

            start.store(true, minstd::memory_order_release);

            size_t total_operations = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
            }

            const size_t expected_operations = num_threads * iterations_per_thread;

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));

            CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());

            CHECK_TRUE(list.validate_ordering());
        }
    }

    TEST(SkiplistTests, MultiThreadedStressOrderingAndContentCorrectness)
    {
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        pthread_t workers[SKIPLIST_STRESS_NUM_THREADS]{};
        skiplist_stress_thread_args<list_type> thread_args[SKIPLIST_STRESS_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> validation_failures{0};

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].validation_failures_ = &validation_failures;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = SKIPLIST_STRESS_ITERATIONS_PER_THREAD * 2;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_stress_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < SKIPLIST_STRESS_NUM_THREADS)
        {
            sched_yield();
        }

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        CHECK_EQUAL(SKIPLIST_STRESS_NUM_THREADS * (SKIPLIST_STRESS_ITERATIONS_PER_THREAD * 2), total_operations);
        CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            auto value = list.find(key);
            CHECK_TRUE(value != list.end());
            CHECK_EQUAL(key, value->second);
        }

        CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());

        CHECK_TRUE(list.validate_ordering());
    }

    TEST(SkiplistTests, InterruptNestedReadSectionDepthCorrectness)
    {
        static constexpr uint32_t KEY_COUNT = 64;

        intr_test_list_t list;
        for (uint32_t k = 0; k < KEY_COUNT; ++k)
        {
            CHECK_TRUE(list.insert(k, k));
        }

        s_intr_list = &list;
        s_intr_signal_count = 0;
        s_intr_nested_count = 0;

        struct sigaction sa
        {
        };
        struct sigaction sa_old
        {
        };
        sa.sa_handler = sigusr1_nested_read_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        CHECK_EQUAL(0, sigaction(SIGUSR1, &sa, &sa_old));

        minstd::atomic<bool> bomber_stop{false};
        intr_test_bomber_args bargs{pthread_self(), &bomber_stop};
        pthread_t bomber;
        CHECK_EQUAL(0, pthread_create(&bomber, nullptr, intr_test_bomber_fn, &bargs));

        for (size_t i = 0; s_intr_signal_count < 100; ++i)
        {
            const uint32_t key = static_cast<uint32_t>(i % KEY_COUNT);
            auto value = list.find(key);
            CHECK_TRUE(value != list.end());
            CHECK_EQUAL(key, value->second);
        }

        bomber_stop.store(true, minstd::memory_order_release);
        pthread_join(bomber, nullptr);

        CHECK_TRUE(s_intr_signal_count > 0);

        for (size_t cycle = 0; cycle < 5; ++cycle)
        {
            for (uint32_t k = 0; k < KEY_COUNT; ++k)
            {
                list.remove(k);
            }
            for (uint32_t k = 0; k < KEY_COUNT; ++k)
            {
                CHECK_TRUE(list.insert(k, k + static_cast<uint32_t>(cycle) + 1u));
            }
        }

        CHECK_TRUE(list.validate_ordering());

        s_intr_list = nullptr;
        sigaction(SIGUSR1, &sa_old, nullptr);
    }

    TEST(SkiplistWriteCorrectnessTests, ConcurrentInsertContentOrderingThenSequentialRemove)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        static constexpr size_t WRITE_TEST_NUM_THREADS = 8;
        static constexpr size_t WRITE_TEST_ITERATIONS_PER_THREAD = 4096;
        static constexpr uint32_t WRITE_TEST_KEY_SPACE = 2048;

        list_type list;

        bool expected_present[WRITE_TEST_KEY_SPACE]{};

        pthread_t workers[WRITE_TEST_NUM_THREADS]{};
        skiplist_write_correctness_thread_args<list_type> thread_args[WRITE_TEST_NUM_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> validation_failures{0};

        for (size_t i = 0; i < WRITE_TEST_NUM_THREADS; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].validation_failures_ = &validation_failures;
            thread_args[i].expected_present_ = expected_present;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].num_threads_ = static_cast<uint32_t>(WRITE_TEST_NUM_THREADS);
            thread_args[i].iterations_ = WRITE_TEST_ITERATIONS_PER_THREAD;
            thread_args[i].key_space_ = WRITE_TEST_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_write_correctness_worker<list_type>, &thread_args[i]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < WRITE_TEST_NUM_THREADS)
        {
            sched_yield();
        }

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < WRITE_TEST_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        CHECK_EQUAL(WRITE_TEST_NUM_THREADS * WRITE_TEST_ITERATIONS_PER_THREAD, total_operations);
        CHECK_EQUAL(0u, validation_failures.load(minstd::memory_order_relaxed));

        uint32_t expected_size = 0;

        for (uint32_t key = 0; key < WRITE_TEST_KEY_SPACE; ++key)
        {
            auto value = list.find(key);

            if (expected_present[key])
            {
                CHECK_TRUE(value != list.end());
                CHECK_EQUAL(key, value->second);
                expected_size++;
            }
            else
            {
                CHECK_TRUE(value == list.end());
            }
        }

        CHECK_EQUAL(expected_size, list.size());

        CHECK_TRUE(list.validate_ordering());

        for (uint32_t key = 0; key < WRITE_TEST_KEY_SPACE; ++key)
        {
            if (expected_present[key])
            {
                CHECK_TRUE(list.remove(key));
            }
            else
            {
                CHECK_FALSE(list.remove(key));
            }

            CHECK_TRUE(list.find(key) == list.end());
        }

        CHECK_EQUAL(0u, list.size());

        CHECK_TRUE(list.validate_ordering());
    }

    TEST(SkiplistTests, IteratorBasicTest)
    {
        using skip_list_type = minstd::skip_list<uint32_t, uint32_t, 16, 16>;
        skip_list_type list;

        size_t count = 0;
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            count++;
        }
        CHECK_EQUAL(0, count);

        for (uint32_t i = 10; i < 20; ++i)
        {
            list.insert(i, i * 10);
        }

        count = 0;
        uint32_t last_key = 0;
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            count++;
            CHECK_TRUE(it->first > last_key);
            CHECK_EQUAL(it->first * 10, it->second);
            last_key = it->first;
        }
        CHECK_EQUAL(10, count);
        CHECK_EQUAL(19, last_key);

        list.remove(15);
        list.remove(10);
        list.remove(19);

        count = 0;
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            count++;
        }
        CHECK_EQUAL(7, count);
    }
}
