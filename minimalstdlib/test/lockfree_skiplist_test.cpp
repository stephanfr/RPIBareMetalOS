// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <CppUTest/MemoryLeakWarningPlugin.h>

#include <minstdconfig.h>

#include <lockfree/skiplist>
#include <__memory_resource/composite_pool_resource.h>

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

namespace
{
    static constexpr size_t SKIPLIST_STRESS_MAX_THREADS = 16;
    static constexpr size_t SKIPLIST_STRESS_NUM_THREADS = 8;
    static constexpr size_t SKIPLIST_STRESS_ITERATIONS_PER_THREAD = 20000;
    static constexpr uint32_t SKIPLIST_STRESS_KEY_SPACE = 4096;
    static constexpr size_t SKIPLIST_PERF_DEFAULT_MULTIPLIER = 500;
    static constexpr size_t SKIPLIST_MIXED_DEFAULT_MULTIPLIER = 100;
    static constexpr uint32_t WRITE_LOAD_BPS_0_1_PERCENT = 10;
    static constexpr uint32_t WRITE_LOAD_BPS_1_PERCENT = 100;
    static constexpr uint32_t WRITE_LOAD_BPS_5_PERCENT = 500;
    static constexpr uint32_t WRITE_LOAD_BPS_10_PERCENT = 1000;
    static constexpr uint32_t WRITE_LOAD_BPS_20_PERCENT = 2000;
    static constexpr size_t SKIPLIST_COMPOSITE_BUFFER_SIZE = 64 * 1024 * 1024;

    char skiplist_composite_resource_buffer[SKIPLIST_COMPOSITE_BUFFER_SIZE];

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
            return SKIPLIST_STRESS_MAX_THREADS;
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

    struct rwlock_sorted_vector
    {
        rwlock_sorted_vector()
        {
            capacity_ = SKIPLIST_STRESS_KEY_SPACE;
            entries_ = static_cast<entry_type *>(malloc(sizeof(entry_type) * capacity_));

            if (entries_ == nullptr)
            {
                capacity_ = 0;
            }

            pthread_rwlock_init(&lock_, nullptr);
        }

        ~rwlock_sorted_vector()
        {
            pthread_rwlock_wrlock(&lock_);
            free(entries_);
            entries_ = nullptr;
            size_ = 0;
            capacity_ = 0;
            pthread_rwlock_unlock(&lock_);
            pthread_rwlock_destroy(&lock_);
        }

        bool insert(uint32_t key, uint32_t value)
        {
            pthread_rwlock_wrlock(&lock_);

            const size_t position = lower_bound_position(key);

            if ((position < size_) && (entries_[position].key_ == key))
            {
                pthread_rwlock_unlock(&lock_);
                return false;
            }

            if (!ensure_capacity(size_ + 1))
            {
                pthread_rwlock_unlock(&lock_);
                return false;
            }

            if (position < size_)
            {
                memmove(entries_ + position + 1, entries_ + position, sizeof(entry_type) * (size_ - position));
            }

            entries_[position].key_ = key;
            entries_[position].value_ = value;
            size_++;

            pthread_rwlock_unlock(&lock_);
            return true;
        }

        bool remove(uint32_t key)
        {
            pthread_rwlock_wrlock(&lock_);

            const size_t position = lower_bound_position(key);

            if ((position >= size_) || (entries_[position].key_ != key))
            {
                pthread_rwlock_unlock(&lock_);
                return false;
            }

            if (position + 1 < size_)
            {
                memmove(entries_ + position, entries_ + position + 1, sizeof(entry_type) * ((size_ - position) - 1));
            }

            size_--;

            pthread_rwlock_unlock(&lock_);
            return true;
        }

        bool find(uint32_t key, uint32_t &value) const
        {
            pthread_rwlock_rdlock(const_cast<pthread_rwlock_t *>(&lock_));

            const size_t position = lower_bound_position(key);

            if ((position < size_) && (entries_[position].key_ == key))
            {
                value = entries_[position].value_;
                pthread_rwlock_unlock(const_cast<pthread_rwlock_t *>(&lock_));
                return true;
            }

            pthread_rwlock_unlock(const_cast<pthread_rwlock_t *>(&lock_));
            return false;
        }

        struct entry_type
        {
            uint32_t key_;
            uint32_t value_;
        };

        bool ensure_capacity(size_t required)
        {
            if (required <= capacity_)
            {
                return true;
            }

            size_t next_capacity = (capacity_ == 0) ? 16 : capacity_;

            while (next_capacity < required)
            {
                next_capacity *= 2;
            }

            auto *resized = static_cast<entry_type *>(realloc(entries_, sizeof(entry_type) * next_capacity));

            if (resized == nullptr)
            {
                return false;
            }

            entries_ = resized;
            capacity_ = next_capacity;
            return true;
        }

        size_t lower_bound_position(uint32_t key) const
        {
            size_t left = 0;
            size_t right = size_;

            while (left < right)
            {
                const size_t middle = left + ((right - left) / 2);

                if (entries_[middle].key_ < key)
                {
                    left = middle + 1;
                }
                else
                {
                    right = middle;
                }
            }

            return left;
        }

        pthread_rwlock_t lock_{};
        entry_type *entries_ = nullptr;
        size_t size_ = 0;
        size_t capacity_ = 0;
    };

    struct skiplist_benchmark_adapter
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t>;

        bool insert(uint32_t key, uint32_t value)
        {
            return list_.insert(key, value);
        }

        bool remove(uint32_t key)
        {
            return list_.remove(key);
        }

        bool find(uint32_t key, uint32_t &value)
        {
            auto *found = list_.find(key);

            if (found == nullptr)
            {
                return false;
            }

            value = *found;
            return true;
        }

        void finalize()
        {
            list_.gc();
            list_.debug_force_cleanup_for_asan();
        }

        void periodic_maintenance(uint32_t thread_id, size_t iteration)
        {
            if ((thread_id == 0u) && ((iteration & 0x0FFFu) == 0u))
            {
                list_.gc();
            }
        }

        auto debug_get_reclamation_counters() const
        {
            return list_.debug_get_reclamation_counters();
        }

        list_type list_{};
    };

    struct rwlock_sorted_vector_benchmark_adapter
    {
        bool insert(uint32_t key, uint32_t value)
        {
            return list_.insert(key, value);
        }

        bool remove(uint32_t key)
        {
            return list_.remove(key);
        }

        bool find(uint32_t key, uint32_t &value)
        {
            return list_.find(key, value);
        }

        void finalize()
        {
        }

        void periodic_maintenance(uint32_t, size_t)
        {
        }

        rwlock_sorted_vector list_{};
    };

    template <typename adapter_type>
    struct write_load_perf_thread_args
    {
        adapter_type *adapter_ = nullptr;
        minstd::atomic<bool> *start_ = nullptr;
        minstd::atomic<size_t> *ready_count_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        uint32_t write_load_bps_ = 0;
        size_t operations_completed_ = 0;
        size_t read_operations_ = 0;
        size_t write_operations_ = 0;
        size_t insert_successes_ = 0;
        size_t remove_successes_ = 0;
        uint64_t checksum_ = 0;
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
            auto *value = args->list_->find(key);

            if ((value == nullptr) || (*value != key))
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
                auto *value = args->list_->find(key);

                if ((value != nullptr) && (*value != key))
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
            auto *value = args->list_->find(key);

            if (value != nullptr)
            {
                checksum += *value;
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
            auto *value = args->list_->find(key);

            if (value != nullptr)
            {
                checksum += *value;
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

    template <typename adapter_type>
    void *write_load_perf_worker(void *arg)
    {
        auto *args = static_cast<write_load_perf_thread_args<adapter_type> *>(arg);

        args->ready_count_->fetch_add(1, minstd::memory_order_release);

        while (!args->start_->load(minstd::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t read_operations = 0;
        size_t write_operations = 0;
        size_t insert_successes = 0;
        size_t remove_successes = 0;

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t seed = static_cast<uint32_t>(i) ^ ((args->thread_id_ + 1u) * 0x9E3779B9u);

            uint32_t op_mixed = seed + 0x7F4A7C15u;
            op_mixed ^= op_mixed >> 16;
            op_mixed *= 0x85EBCA6Bu;
            op_mixed ^= op_mixed >> 13;
            op_mixed *= 0xC2B2AE35u;
            op_mixed ^= op_mixed >> 16;

            uint32_t key_mixed = seed + 0x51ED270Bu;
            key_mixed ^= key_mixed >> 15;
            key_mixed *= 0x7FEB352Du;
            key_mixed ^= key_mixed >> 12;
            key_mixed *= 0x846CA68Bu;
            key_mixed ^= key_mixed >> 16;

            const uint32_t key = key_mixed % args->key_space_;
            const uint32_t op_roll = op_mixed % 10000u;

            if (op_roll < args->write_load_bps_)
            {
                write_operations++;

                if (((op_mixed >> 10) & 1u) == 0u)
                {
                    if (args->adapter_->insert(key, key))
                    {
                        insert_successes++;
                    }
                }
                else
                {
                    if (args->adapter_->remove(key))
                    {
                        remove_successes++;
                    }
                }
            }
            else
            {
                read_operations++;
                uint32_t value = 0;

                if (args->adapter_->find(key, value))
                {
                    checksum += value;
                }
            }

            args->adapter_->periodic_maintenance(args->thread_id_, i + 1u);
            args->operations_completed_++;
        }

        args->read_operations_ = read_operations;
        args->write_operations_ = write_operations;
        args->insert_successes_ = insert_successes;
        args->remove_successes_ = remove_successes;
        args->checksum_ = checksum;

        return nullptr;
    }

    struct write_load_perf_result
    {
        size_t threads_ = 0;
        size_t iterations_per_thread_ = 0;
        uint32_t write_load_bps_ = 0;
        size_t total_operations_ = 0;
        size_t total_read_operations_ = 0;
        size_t total_write_operations_ = 0;
        size_t total_insert_successes_ = 0;
        size_t total_remove_successes_ = 0;
        uint64_t checksum_ = 0;
        double duration_seconds_ = 0.0;
        double ops_per_sec_ = 0.0;
    };

    template <typename adapter_type>
    write_load_perf_result run_write_load_perf_case(const char *label, uint32_t write_load_bps)
    {
        const size_t threads = skiplist_perf_thread_count();
        const size_t iterations_per_thread = skiplist_mixed_iterations_per_thread();

        adapter_type adapter;

        const uint32_t prefill_count = static_cast<uint32_t>((SKIPLIST_STRESS_KEY_SPACE * 9u) / 10u);

        for (uint32_t key = 0; key < prefill_count; ++key)
        {
            CHECK_TRUE(adapter.insert(key, key));
        }

        pthread_t workers[SKIPLIST_STRESS_MAX_THREADS]{};
        write_load_perf_thread_args<adapter_type> thread_args[SKIPLIST_STRESS_MAX_THREADS]{};

        minstd::atomic<bool> start{false};
        minstd::atomic<size_t> ready_count{0};

        for (size_t index = 0; index < threads; ++index)
        {
            thread_args[index].adapter_ = &adapter;
            thread_args[index].start_ = &start;
            thread_args[index].ready_count_ = &ready_count;
            thread_args[index].thread_id_ = static_cast<uint32_t>(index);
            thread_args[index].iterations_ = iterations_per_thread;
            thread_args[index].key_space_ = SKIPLIST_STRESS_KEY_SPACE;
            thread_args[index].write_load_bps_ = write_load_bps;

            CHECK_EQUAL(0, pthread_create(&workers[index], nullptr, write_load_perf_worker<adapter_type>, &thread_args[index]));
        }

        while (ready_count.load(minstd::memory_order_acquire) < threads)
        {
            sched_yield();
        }

        timespec start_time{};
        timespec end_time{};
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start.store(true, minstd::memory_order_release);

        write_load_perf_result result;
        result.threads_ = threads;
        result.iterations_per_thread_ = iterations_per_thread;
        result.write_load_bps_ = write_load_bps;

        for (size_t index = 0; index < threads; ++index)
        {
            CHECK_EQUAL(0, pthread_join(workers[index], nullptr));
            result.total_operations_ += thread_args[index].operations_completed_;
            result.total_read_operations_ += thread_args[index].read_operations_;
            result.total_write_operations_ += thread_args[index].write_operations_;
            result.total_insert_successes_ += thread_args[index].insert_successes_;
            result.total_remove_successes_ += thread_args[index].remove_successes_;
            result.checksum_ += thread_args[index].checksum_;
        }

        if constexpr (requires(const adapter_type &adapter_ref) { adapter_ref.debug_get_reclamation_counters(); })
        {
            const auto reclamation = adapter.debug_get_reclamation_counters();

            printf("Reclaim telemetry: impl=%s write_load=%.1f%% epoch=%llu retired=%llu reclaim_calls=%llu reclaimed=%llu eligible=%llu ineligible=%llu pending_total=%llu pending_max_slot=%u\n",
                   label,
                   static_cast<double>(write_load_bps) / 100.0,
                   static_cast<unsigned long long>(reclamation.current_epoch),
                   static_cast<unsigned long long>(reclamation.retire_enqueued_nodes),
                   static_cast<unsigned long long>(reclamation.reclaim_invocations),
                   static_cast<unsigned long long>(reclamation.reclaim_total_reclaimed),
                   static_cast<unsigned long long>(reclamation.reclaim_scan_eligible),
                   static_cast<unsigned long long>(reclamation.reclaim_scan_ineligible),
                   static_cast<unsigned long long>(reclamation.total_pending_nodes),
                   static_cast<unsigned int>(reclamation.max_slot_pending_nodes));
        }

        adapter.finalize();

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        result.duration_seconds_ = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                   (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        result.ops_per_sec_ = static_cast<double>(result.total_operations_) / result.duration_seconds_;

        const size_t expected_operations = threads * iterations_per_thread;
        const double write_load_percent = static_cast<double>(write_load_bps) / 100.0;

        printf("Write-load perf: impl=%s write_load=%.1f%% threads=%zu iterations/thread=%zu ops/sec=%f reads=%zu writes=%zu inserts=%zu removes=%zu\n",
               label,
               write_load_percent,
               threads,
               iterations_per_thread,
               result.ops_per_sec_,
               result.total_read_operations_,
               result.total_write_operations_,
               result.total_insert_successes_,
               result.total_remove_successes_);

        CHECK_EQUAL(expected_operations, result.total_operations_);
        CHECK_EQUAL(expected_operations, result.total_read_operations_ + result.total_write_operations_);
        CHECK_TRUE(result.duration_seconds_ > 0.0);
        CHECK_TRUE(result.ops_per_sec_ > 0.0);
        CHECK_TRUE(result.checksum_ > 0);

        return result;
    }

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

    TEST(SkiplistTests, BasicFunctionality)
    {
        minstd::SkipList<uint32_t, uint32_t> list;

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK_TRUE(list.insert(i, i));
        }

        CHECK_EQUAL(100u, list.size());

        for (uint32_t i = 0; i < 100; i++)
        {
            CHECK_TRUE(list.find(i) != nullptr);
            CHECK_EQUAL(i, *list.find(i));
        }

        CHECK_TRUE(list.find(101) == nullptr);

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
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }

        CHECK_FALSE(list.insert(14, 114));
        CHECK_EQUAL(14u, *list.find(14));

        CHECK_FALSE(list.insert(37, 137));
        CHECK_EQUAL(37u, *list.find(37));

        CHECK_TRUE(list.insert(100, 100));

        CHECK_EQUAL(81u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }

        list.gc();
        list.gc();

        CHECK_EQUAL(81u, list.size());

        for (uint32_t i = 0; i <= 100; i++)
        {
            if (((i % 10 == 0) || ((i > 0) && ((i - 1) % 10 == 0))) && (i != 100))
            {
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
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
                CHECK_TRUE(list.find(i) == nullptr);
            }
            else
            {
                CHECK_TRUE(list.find(i) != nullptr);
                CHECK_EQUAL(i, *list.find(i));
            }
        }
    }

    TEST(SkiplistTests, TemplateInstantiationWithCustomMaxLevels)
    {
        minstd::SkipList<uint32_t, uint64_t, 8> list;

        for (uint32_t i = 0; i < 256; ++i)
        {
            CHECK_TRUE(list.insert(i, static_cast<uint64_t>(i) * 10ull));
        }

        CHECK_EQUAL(256u, list.size());

        for (uint32_t i = 0; i < 256; ++i)
        {
            auto *value = list.find(i);
            CHECK_TRUE(value != nullptr);
            CHECK_EQUAL(static_cast<uint64_t>(i) * 10ull, *value);
        }

        for (uint32_t i = 0; i < 128; ++i)
        {
            CHECK_TRUE(list.remove(i));
        }

        list.gc();
        list.gc();

        for (uint32_t i = 0; i < 128; ++i)
        {
            CHECK_TRUE(list.find(i) == nullptr);
        }

        for (uint32_t i = 128; i < 256; ++i)
        {
            auto *value = list.find(i);
            CHECK_TRUE(value != nullptr);
            CHECK_EQUAL(static_cast<uint64_t>(i) * 10ull, *value);
        }
    }

    TEST(SkiplistTests, MultiThreadedStressInsertFindRemove)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t>;

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

        timespec start_time{};
        timespec end_time{};
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        start.store(true, minstd::memory_order_release);

        size_t total_operations = 0;

        for (size_t i = 0; i < SKIPLIST_STRESS_NUM_THREADS; ++i)
        {
            CHECK_EQUAL(0, pthread_join(workers[i], nullptr));
            total_operations += thread_args[i].operations_completed_;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        const size_t expected_operations = SKIPLIST_STRESS_NUM_THREADS * SKIPLIST_STRESS_ITERATIONS_PER_THREAD;
        const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
        const double ops_per_sec = static_cast<double>(total_operations) / duration;

        printf("Skiplist stress (fixed %zu threads): %f ops/sec\n", SKIPLIST_STRESS_NUM_THREADS, ops_per_sec);

        CHECK_EQUAL(expected_operations, total_operations);
        CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));
        CHECK_TRUE(duration > 0.0);
        CHECK_TRUE(ops_per_sec > 0.0);

        CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());
    }

    TEST(SkiplistTests, MultiThreadedStressThreadScaling)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t>;
        const size_t iterations_per_thread = skiplist_scaling_iterations_per_thread();

        printf("Skiplist concurrent find ops/sec (iterations/thread=%zu):\n", iterations_per_thread);

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

            const size_t expected_operations = num_threads * iterations_per_thread;
            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  %2zu threads: %f\n", num_threads, ops_per_sec);

            CHECK_EQUAL(expected_operations, total_operations);
            CHECK_EQUAL(static_cast<size_t>(0), validation_failures.load(minstd::memory_order_acquire));
            CHECK_TRUE(duration > 0.0);
            CHECK_TRUE(ops_per_sec > 0.0);

            CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());

#ifdef __MINIMAL_STD_TEST__
            CHECK_TRUE(list.debug_validate_ordering());
#endif
        }
    }

    TEST(SkiplistPerformanceTests, PerfOnlyFindThreadScaling)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t>;
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
        using list_type = minstd::SkipList<uint32_t, uint32_t>;

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

        using list_type = minstd::SkipList<uint32_t, uint32_t>;

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

    TEST(SkiplistPerformanceTests, PerfReadWriteLoadMatrixSkiplistVsRwSortedVector)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        const uint32_t write_loads_bps[] = {
            WRITE_LOAD_BPS_0_1_PERCENT,
            WRITE_LOAD_BPS_1_PERCENT,
            WRITE_LOAD_BPS_5_PERCENT,
            WRITE_LOAD_BPS_10_PERCENT,
            WRITE_LOAD_BPS_20_PERCENT
        };

        printf("Write-load benchmark matrix (skiplist vs rwlock-sorted-vector):\n");

        for (size_t index = 0; index < (sizeof(write_loads_bps) / sizeof(write_loads_bps[0])); ++index)
        {
            const uint32_t write_load_bps = write_loads_bps[index];
            const double write_load_percent = static_cast<double>(write_load_bps) / 100.0;

            const auto skiplist_result = run_write_load_perf_case<skiplist_benchmark_adapter>("skiplist", write_load_bps);
                 const auto sorted_vector_result = run_write_load_perf_case<rwlock_sorted_vector_benchmark_adapter>("rwlock_sorted_vector", write_load_bps);

                 const double ratio = skiplist_result.ops_per_sec_ / sorted_vector_result.ops_per_sec_;

                 printf("Write-load summary: write_load=%.1f%% skiplist_ops/sec=%f rwvector_ops/sec=%f ratio=%f\n",
                   write_load_percent,
                   skiplist_result.ops_per_sec_,
                     sorted_vector_result.ops_per_sec_,
                   ratio);

            CHECK_TRUE(skiplist_result.total_write_operations_ > 0);
                 CHECK_TRUE(sorted_vector_result.total_write_operations_ > 0);
        }
    }

    TEST(SkiplistTests, MultiThreadedStressOrderingAndContentCorrectness)
    {
        using list_type = minstd::SkipList<uint32_t, uint32_t>;

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
            auto *value = list.find(key);
            CHECK_TRUE(value != nullptr);
            CHECK_EQUAL(key, *value);
        }

        CHECK_EQUAL(SKIPLIST_STRESS_KEY_SPACE, list.size());

#ifdef __MINIMAL_STD_TEST__
        CHECK_TRUE(list.debug_validate_ordering());
#endif
    }

    TEST(SkiplistWriteCorrectnessTests, ConcurrentInsertContentOrderingThenSequentialRemove)
    {
        memory_leak_overload_scope_guard memory_leak_overload_guard;

        using list_type = minstd::SkipList<uint32_t, uint32_t>;

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

        list.gc();
        list.gc();

        uint32_t expected_size = 0;

        for (uint32_t key = 0; key < WRITE_TEST_KEY_SPACE; ++key)
        {
            auto *value = list.find(key);

            if (expected_present[key])
            {
                CHECK_TRUE(value != nullptr);
                CHECK_EQUAL(key, *value);
                expected_size++;
            }
            else
            {
                CHECK_TRUE(value == nullptr);
            }
        }

        CHECK_EQUAL(expected_size, list.size());

#ifdef __MINIMAL_STD_TEST__
        CHECK_TRUE(list.debug_validate_ordering());
#endif

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

            CHECK_TRUE(list.find(key) == nullptr);
        }

        list.gc();
        list.gc();

        CHECK_EQUAL(0u, list.size());

    #ifdef __MINIMAL_STD_TEST__
        CHECK_TRUE(list.debug_validate_ordering());
    #endif
    }
}
