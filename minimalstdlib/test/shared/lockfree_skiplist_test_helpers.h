// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <CppUTest/MemoryLeakWarningPlugin.h>
#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/lockfree_composite_single_arena_resource.h>
#include <__memory_resource/malloc_free_wrapper_memory_resource.h>
#include <__memory_resource/polymorphic_allocator.h>
#include <avl_tree>
#include <lockfree/__extensions/skiplist_statistics.h>
#include <lockfree/skiplist>

#include "interrupt_simulation_test_helpers.h"

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

    [[maybe_unused]] size_t skiplist_scaling_iterations_per_thread()
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

    [[maybe_unused]] size_t skiplist_mixed_iterations_per_thread()
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

    [[maybe_unused]] size_t skiplist_perf_thread_count()
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
        using node_allocator_type = minstd::pmr::polymorphic_allocator<map_type::node_type>;

        //  Members declared in strict construction order.  The map destructor runs
        //  before heap_resource_ is torn down, returning all nodes to the upstream
        //  malloc/free wrapper.
        minstd::pmr::malloc_free_wrapper_memory_resource heap_resource_;
        node_allocator_type map_alloc_;
        map_type map_;
        pthread_rwlock_t rwlock_;

        rwlock_hash_map()
            : heap_resource_(),
              map_alloc_(&heap_resource_),
              map_(map_alloc_)
        {
            pthread_rwlock_init(&rwlock_, nullptr);
        }

        ~rwlock_hash_map()
        {
            //  pthread_rwlock destroyed first; member destructors then run in reverse
            //  declaration order: map_ → map_alloc_ → heap_resource_.
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

    template <typename map_type>
    struct rwlock_map_write_load_thread_args
    {
        map_type *map_ = nullptr;
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

    template <typename map_type>
    void *rwlock_map_write_load_worker(void *arg)
    {
        auto *args = static_cast<rwlock_map_write_load_thread_args<map_type> *>(arg);

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

    // ---- Interrupt-nested read section validation scaffolding ---------------------------
    // A background "signal bomber" thread fires SIGUSR1 at the test thread while it
    // loops calling find().  When SIGUSR1 arrives during an active read section (inside
    // find()), the handler's own find() creates a nested read section — exactly the LIFO
    // pattern a bare-metal hardware IRQ would cause.
    using intr_test_list_t = minstd::skip_list<uint32_t, uint32_t, 8>;
    static intr_test_list_t *s_intr_list = nullptr;
    static volatile sig_atomic_t s_intr_signal_count = 0;
    static volatile sig_atomic_t s_intr_nested_count = 0;

    [[maybe_unused]] static void sigusr1_nested_read_handler(int)
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

    // -------------------------------------------------------------------------------------
}
