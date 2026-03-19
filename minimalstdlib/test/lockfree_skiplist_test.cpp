// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/MemoryLeakWarningPlugin.h>
#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/composite_pool_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>
#include <avl_tree>
#include <heap_allocator>
#include <lockfree/__extensions/skiplist_statistics.h>
#include <lockfree/skiplist>
#include <single_block_memory_heap>

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

namespace
{
    static constexpr size_t SKIPLIST_STRESS_MAX_THREADS = 16;
    static constexpr size_t SKIPLIST_STRESS_NUM_THREADS = 8;
    static constexpr size_t SKIPLIST_STRESS_ITERATIONS_PER_THREAD = 20000;
    static constexpr uint32_t SKIPLIST_STRESS_KEY_SPACE = 4096;
    static constexpr size_t SKIPLIST_PERF_DEFAULT_MULTIPLIER = 500;
    static constexpr size_t SKIPLIST_MIXED_DEFAULT_MULTIPLIER = 10;
    static constexpr size_t SKIPLIST_COMPOSITE_BUFFER_SIZE = 64 * 1024 * 1024;
    static constexpr size_t SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT = 50;

    char *skiplist_composite_resource_buffer = new char[SKIPLIST_COMPOSITE_BUFFER_SIZE]();

    size_t skiplist_scaling_iterations_per_thread()
    {
        const char *multiplier_text = getenv("SKIPLIST_STRESS_ITERATIONS_MULTIPLIER");

        const size_t default_iterations_per_thread = SKIPLIST_STRESS_ITERATIONS_PER_THREAD * SKIPLIST_PERF_DEFAULT_MULTIPLIER;

        if ((multiplier_text == nullptr) || (multiplier_text[0] == '\0'))
        {
            return default_iterations_per_thread;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_multiplier = strtoul(multiplier_text, &parse_end, 10);

        if ((parse_end == multiplier_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return default_iterations_per_thread;
        }

        if (parsed_multiplier == 0ul)
        {
            return default_iterations_per_thread;
        }

        constexpr unsigned long max_multiplier = 10000ul;
        const unsigned long clamped_multiplier = (parsed_multiplier > max_multiplier) ? max_multiplier : parsed_multiplier;

        return SKIPLIST_STRESS_ITERATIONS_PER_THREAD * static_cast<size_t>(clamped_multiplier);
    }

    size_t skiplist_mixed_iterations_per_thread()
    {
        const char *multiplier_text = getenv("SKIPLIST_MIXED_ITERATIONS_MULTIPLIER");

        const size_t default_iterations_per_thread = SKIPLIST_STRESS_ITERATIONS_PER_THREAD * SKIPLIST_MIXED_DEFAULT_MULTIPLIER;

        if ((multiplier_text == nullptr) || (multiplier_text[0] == '\0'))
        {
            return default_iterations_per_thread;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_multiplier = strtoul(multiplier_text, &parse_end, 10);

        if ((parse_end == multiplier_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return default_iterations_per_thread;
        }

        if (parsed_multiplier == 0ul)
        {
            return default_iterations_per_thread;
        }

        constexpr unsigned long max_multiplier = 100ul;
        const unsigned long clamped_multiplier = (parsed_multiplier > max_multiplier) ? max_multiplier : parsed_multiplier;

        return SKIPLIST_STRESS_ITERATIONS_PER_THREAD * static_cast<size_t>(clamped_multiplier);
    }

    size_t skiplist_perf_num_arenas()
    {
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

        if (cpu_count < 1)
        {
            return 1;
        }

        return static_cast<size_t>(cpu_count);
    }

    size_t skiplist_perf_thread_count()
    {
        const char *thread_count_text = getenv("SKIPLIST_PERF_THREADS");

        if ((thread_count_text == nullptr) || (thread_count_text[0] == '\0'))
        {
            return SKIPLIST_STRESS_NUM_THREADS;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_thread_count = strtoul(thread_count_text, &parse_end, 10);

        if ((parse_end == thread_count_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return SKIPLIST_STRESS_MAX_THREADS;
        }

        if (parsed_thread_count == 0ul)
        {
            return 1;
        }

        if (parsed_thread_count > SKIPLIST_STRESS_MAX_THREADS)
        {
            return SKIPLIST_STRESS_MAX_THREADS;
        }

        return static_cast<size_t>(parsed_thread_count);
    }

    template <typename list_type>
    struct skiplist_stress_thread_args
    {
        list_type *list_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *validation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
    };

    template <typename list_type>
    struct skiplist_write_correctness_thread_args
    {
        list_type *list_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *validation_failures_ = nullptr;
        bool *expected_present_ = nullptr;
        uint32_t thread_id_ = 0;
        uint32_t num_threads_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
    };

    template <typename list_type>
    struct skiplist_perf_thread_args
    {
        list_type *list_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        uint64_t checksum_ = 0;
    };

    template <typename list_type>
    struct skiplist_perf_composite_thread_args
    {
        list_type *list_ = nullptr;
        minstd::pmr::memory_resource *memory_resource_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *allocation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        size_t composite_allocations_ = 0;
        uint64_t checksum_ = 0;
    };

    template <typename list_type>
    struct skiplist_perf_write_load_thread_args
    {
        list_type *list_ = nullptr;
        minstd::pmr::memory_resource *memory_resource_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *allocation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t write_period_ = 0; // 0 = reads only; N = 1-in-N ops is a write
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        size_t composite_allocations_ = 0;
        size_t insert_successes_ = 0;
        size_t remove_successes_ = 0;
        uint64_t checksum_ = 0;
        uint64_t rng_seed_ = 0;
    };

    struct memory_leak_overload_scope_guard
    {
        memory_leak_overload_scope_guard()
        {
            MemoryLeakWarningPlugin::saveAndDisableNewDeleteOverloads();
        }

        ~memory_leak_overload_scope_guard()
        {
            MemoryLeakWarningPlugin::restoreNewDeleteOverloads();
        }
    };

    template <typename list_type>
    void *skiplist_stress_worker(void *arg)
    {
        auto *args = static_cast<skiplist_stress_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((i + (args->thread_id_ * 97u)) % args->key_space_);
            auto value = args->list_->find(key);

            if ((value == args->list_->end()) || (value->second != key))
            {
                args->validation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }

            args->operations_completed_++;
        }

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_write_correctness_worker(void *arg)
    {
        auto *args = static_cast<skiplist_write_correctness_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((args->thread_id_ + (i * args->num_threads_)) % args->key_space_);

            if ((i & 1u) == 0u)
            {
                if (args->list_->insert(key, key))
                {
                    args->expected_present_[key] = true;
                }
            }
            else
            {
                if (args->list_->remove(key))
                {
                    args->expected_present_[key] = false;
                }
            }

            if ((i % 8u) == 0u)
            {
                auto value = args->list_->find(key);

                if ((value != args->list_->end()) && (value->second != key))
                {
                    args->validation_failures_->fetch_add(1, minstd::memory_order_relaxed);
                }
            }

            args->operations_completed_++;
        }

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_perf_worker(void *arg)
    {
        auto *args = static_cast<skiplist_perf_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((i + (args->thread_id_ * 97u)) % args->key_space_);
            auto value = args->list_->find(key);

            if (value != args->list_->end())
            {
                checksum += value->second;
            }

            args->operations_completed_++;
        }

        args->checksum_ = checksum;

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_perf_composite_worker(void *arg)
    {
        auto *args = static_cast<skiplist_perf_composite_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t composite_allocations = 0;

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>((i + (args->thread_id_ * 97u)) % args->key_space_);
            auto value = args->list_->find(key);

            if (value != args->list_->end())
            {
                checksum += value->second;
            }

            const size_t block_size = 32 + static_cast<size_t>((key + static_cast<uint32_t>(i)) & 0xFFu);
            void *ptr = args->memory_resource_->allocate(block_size);

            if (ptr == nullptr)
            {
                args->allocation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }
            else
            {
                args->memory_resource_->deallocate(ptr, block_size);
                composite_allocations++;
            }

            args->operations_completed_++;
        }

        args->composite_allocations_ = composite_allocations;
        args->checksum_ = checksum;

        return nullptr;
    }

    template <typename list_type>
    void *skiplist_perf_write_load_worker(void *arg)
    {
        auto *args = static_cast<skiplist_perf_write_load_thread_args<list_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t composite_allocations = 0;
        size_t insert_successes = 0;
        size_t remove_successes = 0;

        uint64_t rng_state = args->rng_seed_;

        if (rng_state == 0)
        {
            rng_state = 0x9e3779b97f4a7c15ull ^
                        (static_cast<uint64_t>(args->thread_id_) << 32) ^
                        static_cast<uint64_t>(args->iterations_);
        }

        auto next_random = [&rng_state]() -> uint64_t
        {
            rng_state ^= (rng_state << 13);
            rng_state ^= (rng_state >> 7);
            rng_state ^= (rng_state << 17);
            return rng_state;
        };

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>(next_random() % static_cast<uint64_t>(args->key_space_));

            const size_t block_size = 32 + static_cast<size_t>(next_random() & 0xFFu);
            void *ptr = args->memory_resource_->allocate(block_size);

            if (ptr == nullptr)
            {
                args->allocation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }
            else
            {
                args->memory_resource_->deallocate(ptr, block_size);
                composite_allocations++;
            }

            const bool do_write = (args->write_period_ > 0) && ((next_random() % args->write_period_) == 0);

            if (!do_write)
            {
                auto value = args->list_->find(key);

                if (value != args->list_->end())
                {
                    checksum += value->second;
                }
            }
            else
            {
                if ((next_random() & 1ull) == 0ull)
                {
                    if (args->list_->remove(key))
                    {
                        remove_successes++;
                    }
                }
                else
                {
                    if (args->list_->insert(key, key))
                    {
                        insert_successes++;
                    }
                }
            }

            args->operations_completed_++;
        }

        args->composite_allocations_ = composite_allocations;
        args->insert_successes_ = insert_successes;
        args->remove_successes_ = remove_successes;
        args->checksum_ = checksum;

        return nullptr;
    }

    //
    //  Reader/writer lock protected ordered map baseline
    //

    struct rwlock_hash_map
    {
        using map_type = minstd::avl_tree<uint32_t, uint32_t>;
        using node_allocator_type = minstd::heap_allocator<map_type::node_type>;

        //  Allow for the full key space plus single_block_memory_heap block header overhead per node.
        static constexpr size_t HEAP_BUFFER_BYTES = SKIPLIST_STRESS_KEY_SPACE * 128;

        //  RAII guard for the malloc'd heap buffer.  Declared FIRST so it is constructed first
        //  and destroyed LAST — after ~map_type() has returned all nodes to the heap.
        struct heap_buffer_guard
        {
            uint8_t *const ptr;
            explicit heap_buffer_guard(size_t size) : ptr(static_cast<uint8_t *>(malloc(size))) {}
            ~heap_buffer_guard() { free(ptr); }
        };

        //  Members declared in strict construction order.
        heap_buffer_guard heap_buf_; //  first in, last out
        minstd::single_block_memory_heap heap_;
        node_allocator_type map_alloc_;
        map_type map_;
        pthread_rwlock_t rwlock_;

        rwlock_hash_map()
            : heap_buf_(HEAP_BUFFER_BYTES),
              heap_(heap_buf_.ptr, HEAP_BUFFER_BYTES),
              map_alloc_(heap_),
              map_(map_alloc_)
        {
            pthread_rwlock_init(&rwlock_, nullptr);
        }

        ~rwlock_hash_map()
        {
            //  pthread_rwlock destroyed first; member destructors then run in reverse
            //  declaration order: map_ → map_alloc_ → heap_ → heap_buf_ (free).
            pthread_rwlock_destroy(&rwlock_);
        }

        bool find(uint32_t key, uint32_t &out_value)
        {
            pthread_rwlock_rdlock(&rwlock_);
            auto it = map_.find(key);
            const bool found = (it != map_.end());
            if (found)
            {
                out_value = minstd::get<1>(*it);
            }
            pthread_rwlock_unlock(&rwlock_);
            return found;
        }

        bool insert(uint32_t key, uint32_t value)
        {
            pthread_rwlock_wrlock(&rwlock_);
            const auto result = map_.insert(key, value);
            pthread_rwlock_unlock(&rwlock_);
            return result.second;
        }

        bool remove(uint32_t key)
        {
            pthread_rwlock_wrlock(&rwlock_);
            const size_t erased = map_.erase(key);
            pthread_rwlock_unlock(&rwlock_);
            return erased > 0;
        }
    };

    struct rwlock_map_write_load_thread_args
    {
        rwlock_hash_map *map_ = nullptr;
        minstd::pmr::memory_resource *memory_resource_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        minstd::atomic<size_t> *allocation_failures_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t write_period_ = 0; // 0 = reads only; N = 1-in-N ops is a write
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        size_t composite_allocations_ = 0;
        size_t insert_successes_ = 0;
        size_t remove_successes_ = 0;
        uint64_t checksum_ = 0;
        uint64_t rng_seed_ = 0;
    };

    void *rwlock_map_write_load_worker(void *arg)
    {
        auto *args = static_cast<rwlock_map_write_load_thread_args *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t composite_allocations = 0;
        size_t insert_successes = 0;
        size_t remove_successes = 0;

        uint64_t rng_state = args->rng_seed_;

        if (rng_state == 0)
        {
            rng_state = 0x9e3779b97f4a7c15ull ^
                        (static_cast<uint64_t>(args->thread_id_) << 32) ^
                        static_cast<uint64_t>(args->iterations_);
        }

        auto next_random = [&rng_state]() -> uint64_t
        {
            rng_state ^= (rng_state << 13);
            rng_state ^= (rng_state >> 7);
            rng_state ^= (rng_state << 17);
            return rng_state;
        };

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>(next_random() % static_cast<uint64_t>(args->key_space_));

            const size_t block_size = 32 + static_cast<size_t>(next_random() & 0xFFu);
            void *ptr = args->memory_resource_->allocate(block_size);

            if (ptr == nullptr)
            {
                args->allocation_failures_->fetch_add(1, minstd::memory_order_relaxed);
            }
            else
            {
                args->memory_resource_->deallocate(ptr, block_size);
                composite_allocations++;
            }

            const bool do_write = (args->write_period_ > 0) && ((next_random() % args->write_period_) == 0);

            if (!do_write)
            {
                uint32_t found_value = 0;

                if (args->map_->find(key, found_value))
                {
                    checksum += found_value;
                }
            }
            else
            {
                if ((next_random() & 1ull) == 0ull)
                {
                    if (args->map_->remove(key))
                    {
                        remove_successes++;
                    }
                }
                else
                {
                    if (args->map_->insert(key, key))
                    {
                        insert_successes++;
                    }
                }
            }

            args->operations_completed_++;
        }

        args->composite_allocations_ = composite_allocations;
        args->insert_successes_ = insert_successes;
        args->remove_successes_ = remove_successes;
        args->checksum_ = checksum;

        return nullptr;
    }

    // ---- Interrupt-nested read section validation scaffolding (Phase A, Step 1) ----------
    // A background "signal bomber" thread fires SIGUSR1 at the test thread while it
    // loops calling find().  When SIGUSR1 arrives during an active read section (inside
    // find()), the handler's own find() creates a nested read section — exactly the LIFO
    // pattern a bare-metal hardware IRQ would cause.  If reader_depth_ accounting is
    // correct, all increments and decrements balance to 0 and epoch reclaim stays live.
    using intr_test_list_t = minstd::skip_list<uint32_t, uint32_t, 8>;
    static intr_test_list_t *s_intr_list = nullptr;
    static volatile sig_atomic_t s_intr_signal_count = 0;
    static volatile sig_atomic_t s_intr_nested_count = 0;

    static void sigusr1_nested_read_handler(int)
    {
        if (s_intr_list != nullptr)
        {
            auto value = s_intr_list->find(0u);
            if (value != s_intr_list->end())
            {
                // Use assignment form to avoid C++20 deprecated ++volatile warning.
                s_intr_nested_count = s_intr_nested_count + 1;
            }
            s_intr_signal_count = s_intr_signal_count + 1;
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
    // ---------------------------------------------------------------------------------

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SkiplistPerformanceTests)
    {
    };
#pragma GCC diagnostic pop

    TEST(SkiplistTests, BlockGarbageCollection)
    {
        // 1024 slots per block, so shift is 10.
        // Needs minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS, minstd::skiplist_internal::DEFAULT_MAX_LEVELS, 10>
        minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS, 16, 10, minstd::skiplist_extensions::skiplist_statistics> list;

        list.reset_slot_high_water_mark();

        // Check it starts empty block-wise, or maybe 1 initial block is there?
        // Wait, atomic_forward_link starts with no blocks physically allocated until ensure_block_installed.
        // Actually, next_free_slot_=1 so slot 0 is skipped.

        for (uint32_t cycle = 0; cycle < 10; ++cycle)
        {
            // Insert 15,000 items -> requires 15 blocks.
            for (uint32_t i = 1; i <= 15000; ++i)
            {
                CHECK_TRUE(list.insert(i, i));
            }

            CHECK_EQUAL(15000u, list.size());

            // Should have ~15 blocks allocated.
            CHECK_TRUE(list.active_blocks() >= 14);

            // Remove 14,500 items, leaving 500 items.
            // We delete deterministically to empty full blocks. If we delete from the end, earlier blocks are left.
            // But wait, the blocks themselves are allocated dynamically based on `next_free_slot_` or `partially_free_blocks_`.
            // If we remove all, it will definitely empty them!
            for (uint32_t i = 1; i <= 14500; ++i)
            {
                CHECK_TRUE(list.remove(i));
            }

            CHECK_EQUAL(500u, list.size());

            // Force epochs to advance so unlinked blocks get fully deleted.
            // On a single-threaded test, advancing the epoch is enough.
            for (uint32_t i = 0; i < 50; ++i)
            {
                list.try_advance_epoch(0); // slot 0 might not be valid wait...
                list.find(0xFFFFFFFF);     // Traversal participates in epochs
            }

            // Because it deleted 14500 items, at least 13 blocks must be completely empty and thus returned to the OS.
            // The active blocks should go back down significantly.
            // Since there's only 500 items left, they could all fit in 1 block, although depending on fragmentation it could be slightly more.
            CHECK_TRUE(list.active_blocks() <= 5);

            // Clean up the remaining 500 items for the next cycle
            for (uint32_t i = 14501; i <= 15000; ++i)
            {
                CHECK_TRUE(list.remove(i));
            }

            // Flush again
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
        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();

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

    TEST(SkiplistPerformanceTests, PerfOnlyFindThreadScaling)
    {
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;
        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();

        printf("Skiplist perf-only find ops/sec (iterations/thread=%zu):\n", iterations_per_thread);

        for (size_t num_threads = 1; num_threads <= SKIPLIST_STRESS_MAX_THREADS; ++num_threads)
        {
            list_type list;

            for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
            {
                CHECK_TRUE(list.insert(key, key));
            }

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            skiplist_perf_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].list_ = &list;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;
                thread_args[i].checksum_ = 0;

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_worker<list_type>, &thread_args[i]));
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
            uint64_t checksum = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
                checksum += thread_args[i].checksum_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  %2zu threads: %f\n", num_threads, ops_per_sec);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
            CHECK_TRUE(checksum > 0);
        }
    }

    TEST(SkiplistPerformanceTests, PerfOnlyFindFixedThreadCount)
    {
        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
        skiplist_perf_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};

        for (size_t i = 0; i < num_threads; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = iterations_per_thread;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;
            thread_args[i].checksum_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_worker<list_type>, &thread_args[i]));
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
        uint64_t checksum = 0;

        for (size_t i = 0; i < num_threads; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
            checksum += thread_args[i].checksum_;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        const size_t expected_operations = num_threads * iterations_per_thread;
        const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        const double ops_per_sec = static_cast<double>(total_operations) / duration;

        printf("Skiplist perf-only fixed threads: threads=%zu iterations/thread=%zu ops/sec=%f\n",
               num_threads,
               iterations_per_thread,
               ops_per_sec);

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_TRUE(duration > 0.0);
        CHECK_TRUE(ops_per_sec > 0.0);
        CHECK_TRUE(checksum > 0);
    }

    TEST(SkiplistPerformanceTests, PerfOnlyFindFixedThreadCountWithCompositeResource)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS>;

        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        list_type list;

        for (uint32_t key = 0; key < SKIPLIST_STRESS_KEY_SPACE; ++key)
        {
            CHECK_TRUE(list.insert(key, key));
        }

        minstd::pmr::composite_pool_resource<1000, 64, 1024, 32, 512, false> composite_resource(
            skiplist_composite_resource_buffer,
            SKIPLIST_COMPOSITE_BUFFER_SIZE,
            skiplist_perf_num_arenas());

        pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
        skiplist_perf_composite_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};
        minstd::atomic<size_t> allocation_failures{0};

        for (size_t i = 0; i < num_threads; ++i)
        {
            thread_args[i].list_ = &list;
            thread_args[i].memory_resource_ = &composite_resource;
            thread_args[i].start_ = &start;
            thread_args[i].ready_count_ = &ready_count;
            thread_args[i].allocation_failures_ = &allocation_failures;
            thread_args[i].thread_id_ = static_cast<uint32_t>(i);
            thread_args[i].iterations_ = iterations_per_thread;
            thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[i].operations_completed_ = 0;
            thread_args[i].composite_allocations_ = 0;
            thread_args[i].checksum_ = 0;

            CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_composite_worker<list_type>, &thread_args[i]));
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
        size_t total_composite_allocations = 0;
        uint64_t checksum = 0;

        for (size_t i = 0; i < num_threads; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
            total_composite_allocations += thread_args[i].composite_allocations_;
            checksum += thread_args[i].checksum_;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        const size_t expected_operations = num_threads * iterations_per_thread;
        const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        const double ops_per_sec = static_cast<double>(total_operations) / duration;

        printf("Skiplist + composite perf fixed threads: threads=%zu iterations/thread=%zu find_ops/sec=%f composite_allocations=%zu\n",
               num_threads,
               iterations_per_thread,
               ops_per_sec,
               total_composite_allocations);

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_EQUAL(expected_operations, total_composite_allocations);
        CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
        CHECK_TRUE(duration > 0.0);
        CHECK_TRUE(ops_per_sec > 0.0);
        CHECK_TRUE(checksum > 0);
    }

    TEST(SkiplistPerformanceTests, PerfMixedWriteLoadWithCompositeResource)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::skip_list<uint32_t, uint32_t, SKIPLIST_STRESS_MAX_THREADS, DEFAULT_MAX_LEVELS, 14, minstd::skiplist_extensions::skiplist_statistics>;

        const size_t iterations_per_thread = skiplist_mixed_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        // Write load configurations: label + write_period (1-in-N ops is a write; 0 = reads only)
        struct write_load_config
        {
            const char *label;
            size_t write_period; // 0 = read-only; N = 1-in-N ops is a write
        };

        static const write_load_config configs[] = {
            {"0.1%", 1000},
            {"1%", 100},
            {"5%", 20},
            {"10%", 10},
            {"20%", 5},
        };

        printf("Skiplist + malloc/free wrapper write-load perf: threads=%zu iterations/thread=%zu initial_occupancy=%zu%%\n",
               num_threads, iterations_per_thread, SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT);

        uint32_t overall_slot_high_water = 0;

        for (size_t cfg_idx = 0; cfg_idx < sizeof(configs) / sizeof(configs[0]); ++cfg_idx)
        {
            const write_load_config &cfg = configs[cfg_idx];

            list_type::reset_slot_high_water_mark();

            list_type list;

            const uint32_t initial_count = static_cast<uint32_t>(SKIPLIST_STRESS_KEY_SPACE * SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT / 100u);
            for (uint32_t key = 0; key < initial_count; ++key)
            {
                CHECK_TRUE(list.insert(key, key));
            }

            minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            skiplist_perf_write_load_thread_args<list_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};
            minstd::atomic<size_t> allocation_failures{0};

            timespec seed_time{};
            clock_gettime(CLOCK_MONOTONIC, &seed_time);
            const uint64_t run_seed =
                (static_cast<uint64_t>(seed_time.tv_sec) << 32) ^
                static_cast<uint64_t>(seed_time.tv_nsec) ^
                (static_cast<uint64_t>(cfg_idx) * 0x9e3779b97f4a7c15ull) ^
                static_cast<uint64_t>(cfg.write_period);

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].list_ = &list;
                thread_args[i].memory_resource_ = &malloc_free_resource;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].allocation_failures_ = &allocation_failures;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].write_period_ = cfg.write_period;
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;
                thread_args[i].composite_allocations_ = 0;
                thread_args[i].insert_successes_ = 0;
                thread_args[i].remove_successes_ = 0;
                thread_args[i].checksum_ = 0;
                thread_args[i].rng_seed_ = run_seed ^
                                           (static_cast<uint64_t>(i + 1) * 0xbf58476d1ce4e5b9ull) ^
                                           (static_cast<uint64_t>(thread_args[i].thread_id_) << 17);

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, skiplist_perf_write_load_worker<list_type>, &thread_args[i]));
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
            size_t total_composite_allocations = 0;
            size_t total_insert_successes = 0;
            size_t total_remove_successes = 0;
            uint64_t checksum = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
                total_composite_allocations += thread_args[i].composite_allocations_;
                total_insert_successes += thread_args[i].insert_successes_;
                total_remove_successes += thread_args[i].remove_successes_;
                checksum += thread_args[i].checksum_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            uint32_t slot_high_water = 0;

            slot_high_water = list_type::slot_high_water_mark();
            if (slot_high_water > overall_slot_high_water)
            {
                overall_slot_high_water = slot_high_water;
            }

            printf("  write=%-5s ops/sec=%f inserts=%zu removes=%zu allocs=%zu slot_high_water=%u\n",
                   cfg.label,
                   ops_per_sec,
                   total_insert_successes,
                   total_remove_successes,
                   total_composite_allocations,
                   slot_high_water);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(expected_operations, total_composite_allocations);
            CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
            CHECK_TRUE((total_insert_successes + total_remove_successes) > 0);
            CHECK_TRUE(checksum > 0);
        }

        printf("Skiplist mixed write-load overall slot high-water mark: %u\n", overall_slot_high_water);
        CHECK_TRUE(overall_slot_high_water > 0u);
    }

    TEST(SkiplistPerformanceTests, PerfMixedWriteLoadRWLockMapBaseline)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        const size_t iterations_per_thread = skiplist_mixed_iterations_per_thread();
        const size_t num_threads = skiplist_perf_thread_count();

        struct write_load_config
        {
            const char *label;
            size_t write_period; // 0 = read-only; N = 1-in-N ops is a write
        };

        static const write_load_config configs[] = {
            {"0.1%", 1000},
            {"1%", 100},
            {"5%", 20},
            {"10%", 10},
            {"20%", 5},
        };

        printf("Reader/writer lock ordered map write-load perf (baseline): threads=%zu iterations/thread=%zu initial_occupancy=%zu%%\n",
               num_threads, iterations_per_thread, SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT);

        for (size_t cfg_idx = 0; cfg_idx < sizeof(configs) / sizeof(configs[0]); ++cfg_idx)
        {
            const write_load_config &cfg = configs[cfg_idx];

            rwlock_hash_map map;

            const uint32_t initial_count = static_cast<uint32_t>(SKIPLIST_STRESS_KEY_SPACE * SKIPLIST_WRITE_LOAD_INITIAL_OCCUPANCY_PCT / 100u);
            for (uint32_t key = 0; key < initial_count; ++key)
            {
                CHECK_TRUE(map.insert(key, key));
            }

            minstd::pmr::malloc_free_wrapper_memory_resource malloc_free_resource(nullptr);

            pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
            rwlock_map_write_load_thread_args thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

            minstd::atomic<bool> start{false};
            minstd::atomic<size_t> ready_count{0};
            minstd::atomic<size_t> allocation_failures{0};

            timespec seed_time{};
            clock_gettime(CLOCK_MONOTONIC, &seed_time);
            const uint64_t run_seed =
                (static_cast<uint64_t>(seed_time.tv_sec) << 32) ^
                static_cast<uint64_t>(seed_time.tv_nsec) ^
                (static_cast<uint64_t>(cfg_idx) * 0x9e3779b97f4a7c15ull) ^
                static_cast<uint64_t>(cfg.write_period);

            for (size_t i = 0; i < num_threads; ++i)
            {
                thread_args[i].map_ = &map;
                thread_args[i].memory_resource_ = &malloc_free_resource;
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].allocation_failures_ = &allocation_failures;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].write_period_ = cfg.write_period;
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
                thread_args[i].operations_completed_ = 0;
                thread_args[i].composite_allocations_ = 0;
                thread_args[i].insert_successes_ = 0;
                thread_args[i].remove_successes_ = 0;
                thread_args[i].checksum_ = 0;
                thread_args[i].rng_seed_ = run_seed ^
                                           (static_cast<uint64_t>(i + 1) * 0xbf58476d1ce4e5b9ull) ^
                                           (static_cast<uint64_t>(thread_args[i].thread_id_) << 17);

                CHECK_EQUAL(0, pthread_create(&workers[i], nullptr, rwlock_map_write_load_worker, &thread_args[i]));
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
            size_t total_composite_allocations = 0;
            size_t total_insert_successes = 0;
            size_t total_remove_successes = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
                total_operations += thread_args[i].operations_completed_;
                total_composite_allocations += thread_args[i].composite_allocations_;
                total_insert_successes += thread_args[i].insert_successes_;
                total_remove_successes += thread_args[i].remove_successes_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  write=%-5s ops/sec=%f inserts=%zu removes=%zu allocs=%zu\n",
                   cfg.label,
                   ops_per_sec,
                   total_insert_successes,
                   total_remove_successes,
                   total_composite_allocations);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(expected_operations, total_composite_allocations);
            CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);
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
        // Phase A Step 1: validate that begin_read_section / end_read_section are
        // interrupt-safe under LIFO nesting without interrupt guards.
        //
        // A background signal-bomber thread fires SIGUSR1 at this thread while it
        // loops calling find().  When the signal arrives inside an active find() call
        // (which holds an epoch_read_section_guard), the handler's own find() creates
        // a nested read section.  Correct atomic depth accounting: outer find enters
        // (+1), handler's find enters (+1) and exits (-1), outer find exits (-1) —
        // depth returns to 0 every time.  A phantom residual depth would block epoch
        // advancement, observable via the insert/remove reclaim cycles below.
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

        // At least some signals must have fired during active read sections.
        CHECK_TRUE(s_intr_signal_count > 0);

        // Verify epoch reclaim is unblocked (no phantom readers blocking advancement).
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

    static void* skiplist_soak_peaky_worker_fn(void *arg)
    {
        auto *args = static_cast<skiplist_soak_peaky_worker_args *>(arg);
        
        minstd::Xoroshiro128PlusPlusRNG rng(minstd::Xoroshiro128PlusPlusRNG::Seed(100, args->thread_id_ + 1000));
        
        args->ready_count_->fetch_add(1, minstd::memory_order_release);
        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        size_t ops_since_reconfig = 0;
        uint32_t write_period = 0; // Starts read-only
        const uint32_t possible_write_periods[] = { 5, 10, 50, 100, 1000, 0 };

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

            // Intermittently try full iteration under concurrent mutation conditions
            if (op_val % 10000 == 0)
            {
                size_t count = 0;
                for (const auto& kv : *args->list_)
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


    TEST(SkiplistTests, PeakyDynamicLoadSoakTest)
    {
        const size_t NUM_THREADS = 8;
        const uint32_t KEY_SPACE = 10000;
        
        // Use 1 second for standard 'make test' to keep things fast, but allow an environment variable to override
        size_t SOAK_DURATION_SEC = 1;
        const char* soak_duration_env = std::getenv("SKIPLIST_SOAK_DURATION");
        if (soak_duration_env)
        {
            SOAK_DURATION_SEC = std::atoi(soak_duration_env);
        }
        
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

        minstd::pmr::composite_pool_resource<1000, 64, 1024, 32, 512, true> composite_resource(
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
        for (size_t i = 0; i < NUM_THREADS; ++i) {
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
            
            if (elapsed >= SOAK_DURATION_SEC)
            {
                break;
            }
            size_t current_sec = static_cast<size_t>(elapsed);
            if (current_sec - last_print_sec >= 10)
            {
                size_t current_ops = 0;
                for (size_t i = 0; i < NUM_THREADS; ++i) {
                    current_ops += thread_args[i].operations_completed_;
                }
                printf("  ... elapsed: %zu / %zu seconds (Ops so far: %zu)\n", current_sec, SOAK_DURATION_SEC, current_ops);
                fflush(stdout);
                last_print_sec = current_sec;
            }
            usleep(100000); // 100ms
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
        
        printf("Soak test completed. Operations: %zu, Mem Allocs: %zu, Inserts: %zu, Removes: %zu\n", total_operations, total_composite_allocations, total_inserts, total_removes);
        printf("Pool allocations: %zu, Deallocations: %zu, Current allocated bytes: %zu\n",
               composite_resource.total_allocations(), composite_resource.total_deallocations(),
               composite_resource.current_allocated());
        printf("Signals delivered: %u, Nested reads: %u\n", (uint32_t)s_intr_signal_count, (uint32_t)s_intr_nested_count);
        
        CHECK_EQUAL(static_cast<size_t>(0), allocation_failures.load(minstd::memory_order_acquire));
        CHECK_TRUE(s_intr_signal_count > 0);
        
        CHECK_TRUE(list.validate_ordering());
        
        // Verify no memory leaks occurred in the pool resource
        CHECK_EQUAL(static_cast<size_t>(0), composite_resource.current_allocated());
        CHECK_EQUAL(composite_resource.total_allocations(), composite_resource.total_deallocations());
        
        s_intr_list = nullptr;
    }
}

TEST(SkiplistTests, IteratorBasicTest)
{
    using skip_list_type = minstd::skip_list<uint32_t, uint32_t, 16, 16>;
    skip_list_type list;

    // Empty iteration
    size_t count = 0;
    for (auto it = list.begin(); it != list.end(); ++it)
    {
        count++;
    }
    CHECK_EQUAL(0, count);

    // Insert 10 items
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

    // Delete some to see if iteration handles gaps
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
