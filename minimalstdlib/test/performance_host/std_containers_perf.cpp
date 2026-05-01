// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <pthread.h>
#include <sched.h>
#include <unordered_map>

namespace
{
    static constexpr size_t STRESS_MAX_THREADS = 16;
    static constexpr size_t STRESS_DEFAULT_THREADS = 8;
    static constexpr size_t STRESS_ITERATIONS_PER_THREAD = 20000;
    static constexpr size_t MIXED_DEFAULT_MULTIPLIER = 10;
    static constexpr uint32_t STRESS_KEY_SPACE = 4096;
    static constexpr size_t WRITE_LOAD_INITIAL_OCCUPANCY_PCT = 50;

    struct write_load_config
    {
        const char *label;
        size_t write_period;
    };

    static constexpr write_load_config WRITE_LOAD_CONFIGS[] = {
        {"0.1%", 1000},
        {"1%", 100},
        {"5%", 20},
        {"10%", 10},
        {"20%", 5},
    };

    uint64_t next_random(uint64_t &rng_state)
    {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 7);
        rng_state ^= (rng_state << 17);
        return rng_state;
    }

    size_t parse_thread_count()
    {
        const char *thread_count_text = getenv("SKIPLIST_PERF_THREADS");

        if ((thread_count_text == nullptr) || (thread_count_text[0] == '\0'))
        {
            return STRESS_DEFAULT_THREADS;
        }

        char *parse_end = nullptr;
        const unsigned long parsed_thread_count = strtoul(thread_count_text, &parse_end, 10);

        if ((parse_end == thread_count_text) || ((parse_end != nullptr) && (*parse_end != '\0')))
        {
            return STRESS_DEFAULT_THREADS;
        }

        if (parsed_thread_count == 0ul)
        {
            return 1;
        }

        if (parsed_thread_count > STRESS_MAX_THREADS)
        {
            return STRESS_MAX_THREADS;
        }

        return static_cast<size_t>(parsed_thread_count);
    }

    size_t parse_iterations_per_thread()
    {
        const char *multiplier_text = getenv("SKIPLIST_MIXED_ITERATIONS_MULTIPLIER");
        const size_t default_iterations_per_thread = STRESS_ITERATIONS_PER_THREAD * MIXED_DEFAULT_MULTIPLIER;

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

        return STRESS_ITERATIONS_PER_THREAD * static_cast<size_t>(clamped_multiplier);
    }

    struct rwlock_std_map_wrapper
    {
        std::map<uint32_t, uint32_t> map_;
        pthread_rwlock_t rwlock_{};

        rwlock_std_map_wrapper()
        {
            pthread_rwlock_init(&rwlock_, nullptr);
        }

        ~rwlock_std_map_wrapper()
        {
            pthread_rwlock_destroy(&rwlock_);
        }

        bool find(uint32_t key, uint32_t &out_value)
        {
            pthread_rwlock_rdlock(&rwlock_);
            auto it = map_.find(key);
            const bool found = (it != map_.end());
            if (found)
            {
                out_value = it->second;
            }
            pthread_rwlock_unlock(&rwlock_);
            return found;
        }

        bool insert(uint32_t key, uint32_t value)
        {
            pthread_rwlock_wrlock(&rwlock_);
            const auto result = map_.emplace(key, value);
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

    struct rwlock_std_unordered_map_wrapper
    {
        std::unordered_map<uint32_t, uint32_t> map_;
        pthread_rwlock_t rwlock_{};

        rwlock_std_unordered_map_wrapper()
        {
            map_.reserve(STRESS_KEY_SPACE * 2u);
            pthread_rwlock_init(&rwlock_, nullptr);
        }

        ~rwlock_std_unordered_map_wrapper()
        {
            pthread_rwlock_destroy(&rwlock_);
        }

        bool find(uint32_t key, uint32_t &out_value)
        {
            pthread_rwlock_rdlock(&rwlock_);
            auto it = map_.find(key);
            const bool found = (it != map_.end());
            if (found)
            {
                out_value = it->second;
            }
            pthread_rwlock_unlock(&rwlock_);
            return found;
        }

        bool insert(uint32_t key, uint32_t value)
        {
            pthread_rwlock_wrlock(&rwlock_);
            const auto result = map_.emplace(key, value);
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
    struct write_load_thread_args
    {
        map_type *map_ = nullptr;
        std::atomic<bool> *start_ = nullptr;
        std::atomic<size_t> *ready_count_ = nullptr;
        uint32_t thread_id_ = 0;
        size_t write_period_ = 0;
        size_t iterations_ = 0;
        uint32_t key_space_ = 0;
        size_t operations_completed_ = 0;
        size_t insert_successes_ = 0;
        size_t remove_successes_ = 0;
        uint64_t checksum_ = 0;
        uint64_t rng_seed_ = 0;
    };

    template <typename map_type>
    void *write_load_worker(void *arg)
    {
        auto *args = static_cast<write_load_thread_args<map_type> *>(arg);

        args->ready_count_->fetch_add(1, std::memory_order_release);

        while (!args->start_->load(std::memory_order_acquire))
        {
            sched_yield();
        }

        uint64_t checksum = 0;
        size_t insert_successes = 0;
        size_t remove_successes = 0;

        uint64_t rng_state = args->rng_seed_;
        if (rng_state == 0)
        {
            rng_state = 0x9e3779b97f4a7c15ull ^
                        (static_cast<uint64_t>(args->thread_id_) << 32) ^
                        static_cast<uint64_t>(args->iterations_);
        }

        for (size_t i = 0; i < args->iterations_; ++i)
        {
            const uint32_t key = static_cast<uint32_t>(next_random(rng_state) % args->key_space_);

            const bool do_write = (args->write_period_ != 0) &&
                                  ((next_random(rng_state) % args->write_period_) == 0);

            if (do_write)
            {
                const bool do_insert = ((next_random(rng_state) & 1ull) == 0);
                if (do_insert)
                {
                    if (args->map_->insert(key, key))
                    {
                        insert_successes++;
                    }
                }
                else
                {
                    if (args->map_->remove(key))
                    {
                        remove_successes++;
                    }
                }
            }
            else
            {
                uint32_t out_value = 0;
                if (args->map_->find(key, out_value))
                {
                    checksum += static_cast<uint64_t>(out_value) + 1ull;
                }
            }
        }

        args->operations_completed_ = args->iterations_;
        args->insert_successes_ = insert_successes;
        args->remove_successes_ = remove_successes;
        args->checksum_ = checksum;

        return nullptr;
    }

    template <typename map_type>
    int run_write_load_baseline(const char *label)
    {
        const size_t iterations_per_thread = parse_iterations_per_thread();
        const size_t num_threads = parse_thread_count();

        printf("%s write-load perf: threads=%zu iterations/thread=%zu initial_occupancy=%zu%%\n",
               label,
               num_threads,
               iterations_per_thread,
               WRITE_LOAD_INITIAL_OCCUPANCY_PCT);

        for (size_t cfg_idx = 0; cfg_idx < (sizeof(WRITE_LOAD_CONFIGS) / sizeof(WRITE_LOAD_CONFIGS[0])); ++cfg_idx)
        {
            const write_load_config &cfg = WRITE_LOAD_CONFIGS[cfg_idx];

            map_type map;

            const uint32_t initial_count = static_cast<uint32_t>(STRESS_KEY_SPACE * WRITE_LOAD_INITIAL_OCCUPANCY_PCT / 100u);
            for (uint32_t key = 0; key < initial_count; ++key)
            {
                (void) map.insert(key, key);
            }

            pthread_t workers[STRESS_MAX_THREADS]{};
            write_load_thread_args<map_type> thread_args[STRESS_MAX_THREADS]{};

            std::atomic<bool> start{false};
            std::atomic<size_t> ready_count{0};

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
                thread_args[i].start_ = &start;
                thread_args[i].ready_count_ = &ready_count;
                thread_args[i].thread_id_ = static_cast<uint32_t>(i);
                thread_args[i].write_period_ = cfg.write_period;
                thread_args[i].iterations_ = iterations_per_thread;
                thread_args[i].key_space_ = STRESS_KEY_SPACE;
                thread_args[i].rng_seed_ = run_seed ^
                                           (static_cast<uint64_t>(i + 1) * 0xbf58476d1ce4e5b9ull) ^
                                           (static_cast<uint64_t>(thread_args[i].thread_id_) << 17);

                if (pthread_create(&workers[i], nullptr, write_load_worker<map_type>, &thread_args[i]) != 0)
                {
                    fprintf(stderr, "pthread_create failed\n");
                    return 2;
                }
            }

            while (ready_count.load(std::memory_order_acquire) < num_threads)
            {
                sched_yield();
            }

            timespec start_time{};
            timespec end_time{};
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            start.store(true, std::memory_order_release);

            size_t total_operations = 0;
            size_t total_insert_successes = 0;
            size_t total_remove_successes = 0;
            uint64_t checksum = 0;

            for (size_t i = 0; i < num_threads; ++i)
            {
                if (pthread_join(workers[i], nullptr) != 0)
                {
                    fprintf(stderr, "pthread_join failed\n");
                    return 2;
                }

                total_operations += thread_args[i].operations_completed_;
                total_insert_successes += thread_args[i].insert_successes_;
                total_remove_successes += thread_args[i].remove_successes_;
                checksum += thread_args[i].checksum_;
            }

            clock_gettime(CLOCK_MONOTONIC, &end_time);

            const double duration = static_cast<double>(end_time.tv_sec - start_time.tv_sec) +
                                    (static_cast<double>(end_time.tv_nsec - start_time.tv_nsec) / 1e9);
            const double ops_per_sec = static_cast<double>(total_operations) / duration;

            printf("  write=%-5s ops/sec=%f inserts=%zu removes=%zu checksum=%llu\n",
                   cfg.label,
                   ops_per_sec,
                   total_insert_successes,
                   total_remove_successes,
                   static_cast<unsigned long long>(checksum));
        }

        return 0;
    }
}

int main()
{
    int rc = run_write_load_baseline<rwlock_std_map_wrapper>("Host std::map baseline");
    if (rc != 0)
    {
        return rc;
    }

    rc = run_write_load_baseline<rwlock_std_unordered_map_wrapper>("Host std::unordered_map baseline");
    if (rc != 0)
    {
        return rc;
    }

    return 0;
}
