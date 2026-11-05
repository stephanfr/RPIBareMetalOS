// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Multi-threaded stress tests for the system_stats statistics and
//  SystemStatisticsDB.  Each test spawns POSIX threads and verifies that
//  the implementations hold up under concurrent load.
//
//  Worker functions must be file-scope statics (not lambdas) to satisfy
//  the pthread_create function-pointer requirement.

#include "cpputest_support.h"
#include "stats_db_test_support.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "services/stats_db/system_stats.h"
#include "services/stats_db/stats_db.h"

namespace
{
    using namespace system_stats;

#define CHECK_RESULT(expected, expression) CHECK_EQUAL(static_cast<int>(minstd::lockfree::latest_value_result::expected), static_cast<int>(expression))

    static constexpr uint32_t NUM_THREADS    = 4;
    static constexpr uint32_t OPS_PER_THREAD = 100000;

    // =========================================================================
    //  CounterStatistic — N threads each do M increments; total must equal N*M
    // =========================================================================

    struct counter_worker_arg
    {
        CounterStatistic<200> *stat_;
        uint32_t               operation_count_;
    };

    static void *counter_worker(void *arg)
    {
        auto *worker_arg = static_cast<counter_worker_arg *>(arg);
        for (uint32_t i = 0; i < worker_arg->operation_count_; i++)
        {
            worker_arg->stat_->Increment();
        }
        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(CounterStressTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(CounterStressTests, ConcurrentIncrementsAreAtomic)
    {
        CounterStatistic<200> stat("stress", "ops", "Concurrent increment counter", 0);

        pthread_t          threads[NUM_THREADS];
        counter_worker_arg args[NUM_THREADS];

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            args[i] = {&stat, OPS_PER_THREAD};
            pthread_create(&threads[i], nullptr, counter_worker, &args[i]);
        }
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], nullptr);
        }

        CHECK_EQUAL((int64_t)(NUM_THREADS * OPS_PER_THREAD), stat.Get());
    }

    // =========================================================================
    //  GaugeStatistic — concurrent Set()/Get() produce no torn int64 reads.
    //  Two writers alternate between two distinct bit patterns; N readers
    //  verify every read value matches one of those patterns exactly.
    // =========================================================================

    static constexpr int64_t GAUGE_PATTERN_A = 0x5A5A5A5A5A5A5A5ALL;
    static constexpr int64_t GAUGE_PATTERN_B = 0x3C3C3C3C3C3C3C3CLL;

    struct gauge_writer_arg
    {
        GaugeStatistic<201> *stat_;
        int64_t              value_;
        uint32_t             operation_count_;
    };

    struct gauge_reader_arg
    {
        GaugeStatistic<201> *stat_;
        uint32_t             operation_count_;
        int                  failed_;
    };

    static void *gauge_writer_worker(void *arg)
    {
        auto *worker_arg = static_cast<gauge_writer_arg *>(arg);
        for (uint32_t i = 0; i < worker_arg->operation_count_; i++)
        {
            worker_arg->stat_->Set(worker_arg->value_);
        }
        return nullptr;
    }

    static void *gauge_reader_worker(void *arg)
    {
        auto *worker_arg = static_cast<gauge_reader_arg *>(arg);
        for (uint32_t i = 0; i < worker_arg->operation_count_; i++)
        {
            const int64_t value = worker_arg->stat_->Get();
            if (value != GAUGE_PATTERN_A && value != GAUGE_PATTERN_B)
            {
                worker_arg->failed_ = 1;
                break;
            }
        }
        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(GaugeStressTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(GaugeStressTests, ConcurrentReadsAndWritesProduceNoTornValues)
    {
        GaugeStatistic<201> stat("stress", "gauge", "Concurrent gauge", GAUGE_PATTERN_A);

        // 2 writer threads + NUM_THREADS reader threads
        pthread_t        threads[NUM_THREADS + 2];
        gauge_writer_arg writer_args[2] = {
            {&stat, GAUGE_PATTERN_A, OPS_PER_THREAD},
            {&stat, GAUGE_PATTERN_B, OPS_PER_THREAD}
        };
        gauge_reader_arg reader_args[NUM_THREADS];

        pthread_create(&threads[0], nullptr, gauge_writer_worker, &writer_args[0]);
        pthread_create(&threads[1], nullptr, gauge_writer_worker, &writer_args[1]);
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            reader_args[i] = {&stat, OPS_PER_THREAD, 0};
            pthread_create(&threads[2 + i], nullptr, gauge_reader_worker, &reader_args[i]);
        }

        for (uint32_t i = 0; i < NUM_THREADS + 2; i++)
        {
            pthread_join(threads[i], nullptr);
        }

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK_EQUAL(0, reader_args[i].failed_);
        }
    }

    // =========================================================================
    //  TextStatistic -- NUM_THREADS writers race NUM_THREADS readers on two
    //  slots.  Every string is one character repeated a character-specific
    //  number of times, so a torn read shows up as a mixed or mis-sized string.
    //  Set() never spins: a writer that loses the race gets FAILURE.
    // =========================================================================

    using minstd::lockfree::latest_value_result;

    static constexpr uint32_t TEXT_MAX_LEN = 64;

    static uint32_t text_length_for(char fill_char)
    {
        return 1 + (static_cast<uint32_t>(fill_char - 'a') * 7) % TEXT_MAX_LEN;     //  1..64, crossing 8-byte words
    }

    static bool text_is_consistent(const char *text)
    {
        const char fill_char = text[0];
        if (fill_char < 'a' || fill_char > 'z')
        {
            return false;
        }

        const uint32_t length = text_length_for(fill_char);
        for (uint32_t i = 0; i < length; i++)
        {
            if (text[i] != fill_char)
            {
                return false;
            }
        }

        return text[length] == '\0';
    }

    using StressText = TextStatistic<202, TEXT_MAX_LEN, 2>;

    struct text_writer_arg
    {
        StressText               *stat_;
        uint32_t                  writer_id_;
        uint32_t                  operation_count_;
        uint32_t                  win_count_;
        uint32_t                  unexpected_result_count_;
        minstd::atomic<uint32_t> *writers_done_;
    };

    struct text_reader_arg
    {
        StressText              *stat_;
        minstd::atomic<uint32_t> *writers_done_;
        int                      failed_;
    };

    static void *text_writer_worker(void *arg)
    {
        auto *worker_arg = static_cast<text_writer_arg *>(arg);
        char text_buffer[TEXT_MAX_LEN + 1];

        for (uint32_t i = 0; i < worker_arg->operation_count_; i++)
        {
            const char fill_char = static_cast<char>('a' + (worker_arg->writer_id_ * 7 + i) % 26);
            const uint32_t length = text_length_for(fill_char);
            memset(text_buffer, fill_char, length);
            text_buffer[length] = '\0';

            const latest_value_result result = worker_arg->stat_->Set(text_buffer);
            if (result == latest_value_result::SUCCESS)
            {
                worker_arg->win_count_++;
            }
            else if (result != latest_value_result::FAILURE)
            {
                worker_arg->unexpected_result_count_++;
            }
        }

        worker_arg->writers_done_->fetch_add(1, minstd::memory_order_release);
        return nullptr;
    }

    static void *text_reader_worker(void *arg)
    {
        auto *worker_arg = static_cast<text_reader_arg *>(arg);
        char text_buffer[TEXT_MAX_LEN + 1];

        while (worker_arg->writers_done_->load(minstd::memory_order_acquire) < NUM_THREADS)
        {
            if ((worker_arg->stat_->Get(text_buffer, sizeof(text_buffer)) != latest_value_result::SUCCESS) || !text_is_consistent(text_buffer))
            {
                worker_arg->failed_ = 1;
                break;
            }
        }
        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(TextStatisticStressTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(TextStatisticStressTests, ConcurrentWritersAndReadersNeverSeeTornString)
    {
        StressText stat("stress", "text", "Concurrent text stat");
        stat.Set("a");                  //  prime with a valid value so readers never see EMPTY

        minstd::atomic<uint32_t> writers_done{0};
        pthread_t                threads[NUM_THREADS * 2];
        text_writer_arg          writer_args[NUM_THREADS];
        text_reader_arg          reader_args[NUM_THREADS];

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            reader_args[i] = {&stat, &writers_done, 0};
            pthread_create(&threads[NUM_THREADS + i], nullptr, text_reader_worker, &reader_args[i]);
        }
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            writer_args[i] = {&stat, i, OPS_PER_THREAD / 4, 0, 0, &writers_done};
            pthread_create(&threads[i], nullptr, text_writer_worker, &writer_args[i]);
        }
        for (uint32_t i = 0; i < NUM_THREADS * 2; i++)
        {
            pthread_join(threads[i], nullptr);
        }

        uint32_t total_wins = 0;
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK_EQUAL(0, reader_args[i].failed_);
            CHECK_EQUAL(0u, writer_args[i].unexpected_result_count_);             //  only SUCCESS or FAILURE
            total_wins += writer_args[i].win_count_;
        }

        //  The writer try-lock is unfair, so a given writer may never win; some writer must.

        CHECK_TRUE(total_wins >= 1);

        char text_buffer[TEXT_MAX_LEN + 1];
        CHECK_RESULT(SUCCESS, stat.Get(text_buffer, sizeof(text_buffer)));
        CHECK_TRUE(text_is_consistent(text_buffer));
    }

    // =========================================================================
    //  BlobStatistic -- as for text: each blob is one byte value repeated a
    //  value-specific number of times.
    // =========================================================================

    static constexpr uint32_t BLOB_MAX_BYTES = 64;

    static uint32_t blob_length_for(uint8_t fill_byte)
    {
        return 1 + (static_cast<uint32_t>(fill_byte) * 11) % BLOB_MAX_BYTES;
    }

    //  blob was zeroed before Get(), which copies exactly the stored length.

    static bool blob_is_consistent(const uint8_t *blob)
    {
        const uint8_t fill_byte = blob[0];
        if (fill_byte == 0)
        {
            return false;
        }

        const uint32_t length = blob_length_for(fill_byte);
        for (uint32_t i = 0; i < BLOB_MAX_BYTES; i++)
        {
            if (blob[i] != ((i < length) ? fill_byte : 0))
            {
                return false;
            }
        }

        return true;
    }

    using StressBlob = BlobStatistic<203, BLOB_MAX_BYTES, 2>;

    struct blob_writer_arg
    {
        StressBlob               *stat_;
        uint32_t                  writer_id_;
        uint32_t                  operation_count_;
        uint32_t                  win_count_;
        uint32_t                  unexpected_result_count_;
        minstd::atomic<uint32_t> *writers_done_;
    };

    struct blob_reader_arg
    {
        StressBlob               *stat_;
        minstd::atomic<uint32_t> *writers_done_;
        int                       failed_;
    };

    static void *blob_writer_worker(void *arg)
    {
        auto *worker_arg = static_cast<blob_writer_arg *>(arg);
        uint8_t blob_buffer[BLOB_MAX_BYTES];

        for (uint32_t i = 0; i < worker_arg->operation_count_; i++)
        {
            const uint8_t fill_byte = static_cast<uint8_t>(1 + (worker_arg->writer_id_ * 31 + i) % 255);
            const uint32_t length = blob_length_for(fill_byte);
            memset(blob_buffer, fill_byte, length);

            const latest_value_result result = worker_arg->stat_->Set(blob_buffer, length);
            if (result == latest_value_result::SUCCESS)
            {
                worker_arg->win_count_++;
            }
            else if (result != latest_value_result::FAILURE)
            {
                worker_arg->unexpected_result_count_++;
            }
        }

        worker_arg->writers_done_->fetch_add(1, minstd::memory_order_release);
        return nullptr;
    }

    static void *blob_reader_worker(void *arg)
    {
        auto *worker_arg = static_cast<blob_reader_arg *>(arg);
        uint8_t blob_buffer[BLOB_MAX_BYTES];

        while (worker_arg->writers_done_->load(minstd::memory_order_acquire) < NUM_THREADS)
        {
            memset(blob_buffer, 0, sizeof(blob_buffer));
            if ((worker_arg->stat_->Get(blob_buffer, sizeof(blob_buffer)) != latest_value_result::SUCCESS) || !blob_is_consistent(blob_buffer))
            {
                worker_arg->failed_ = 1;
                break;
            }
        }
        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(BlobStatisticStressTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(BlobStatisticStressTests, ConcurrentWritersAndReadersNeverSeeTornBlob)
    {
        StressBlob stat("stress", "blob", "Concurrent blob stat");
        const uint8_t prime_byte = 1;
        uint8_t prime_buffer[BLOB_MAX_BYTES];
        memset(prime_buffer, prime_byte, blob_length_for(prime_byte));
        stat.Set(prime_buffer, blob_length_for(prime_byte));        //  prime with a valid value so readers never see EMPTY

        minstd::atomic<uint32_t> writers_done{0};
        pthread_t                threads[NUM_THREADS * 2];
        blob_writer_arg          writer_args[NUM_THREADS];
        blob_reader_arg          reader_args[NUM_THREADS];

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            reader_args[i] = {&stat, &writers_done, 0};
            pthread_create(&threads[NUM_THREADS + i], nullptr, blob_reader_worker, &reader_args[i]);
        }
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            writer_args[i] = {&stat, i, OPS_PER_THREAD / 4, 0, 0, &writers_done};
            pthread_create(&threads[i], nullptr, blob_writer_worker, &writer_args[i]);
        }
        for (uint32_t i = 0; i < NUM_THREADS * 2; i++)
        {
            pthread_join(threads[i], nullptr);
        }

        uint32_t total_wins = 0;
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK_EQUAL(0, reader_args[i].failed_);
            CHECK_EQUAL(0u, writer_args[i].unexpected_result_count_);
            total_wins += writer_args[i].win_count_;
        }
        CHECK_TRUE(total_wins >= 1);
    }

    // =========================================================================
    //  SystemStatisticsDB -- concurrent readers against a pre-populated DB.
    // =========================================================================

    GaugeStatistic<2001>     db_gauge    ("stress", "gauge",     "DB stress gauge",   42);
    CounterStatistic<2002>   db_counter  ("stress", "counter",   "DB stress counter", 0);
    TimestampStatistic<2003> db_timestamp("stress", "timestamp", "DB stress timestamp");

    struct db_reader_arg
    {
        SystemStatisticsDB *db_;
        uint32_t            operation_count_;
        int                 failed_;
    };

    static void *db_reader_worker(void *arg)
    {
        auto *worker_arg = static_cast<db_reader_arg *>(arg);
        for (uint32_t i = 0; i < worker_arg->operation_count_; i++)
        {
            if ((FoundStatistic(*worker_arg->db_, 2001) != &db_gauge) ||
                (FoundStatistic(*worker_arg->db_, 2002) != &db_counter) ||
                (FoundStatistic(*worker_arg->db_, 2003) != &db_timestamp))
            {
                worker_arg->failed_ = 1;
                break;
            }
            uint32_t visited_count = 0;
            worker_arg->db_->ForEach([&visited_count](const SystemStatistic &) { visited_count++; return SystemStatisticsDB::VisitAction::CONTINUE; });
            if (visited_count != 3)
            {
                worker_arg->failed_ = 1;
                break;
            }
        }
        return nullptr;
    }

    // =========================================================================
    //  SystemStatisticsDB -- concurrent registration.  The statistics are
    //  SystemStatisticType objects (runtime ids, never statically recorded)
    //  with sparse ids, so many distinct ids need no template instantiations.
    // =========================================================================

    static constexpr uint32_t DB_STAT_COUNT  = 256;
    static constexpr uint32_t DB_ROUNDS      = 20;         //  a fresh database per round
    static constexpr uint32_t DB_REGISTRARS  = NUM_THREADS;
    static constexpr uint32_t DB_READERS     = 2;
    static constexpr uint32_t DB_ID_STRIDE   = 7919;

    static uint32_t db_stat_id(uint32_t index)
    {
        return index * DB_ID_STRIDE + 1;
    }

    struct db_statistic_set
    {
        alignas(SystemStatisticType) uint8_t storage_[DB_STAT_COUNT][sizeof(SystemStatisticType)];
        SystemStatisticType                 *statistics_[DB_STAT_COUNT];

        db_statistic_set()
        {
            for (uint32_t i = 0; i < DB_STAT_COUNT; i++)
            {
                statistics_[i] = new (storage_[i]) SystemStatisticType(SystemStatistic::KERNEL_PROCESS_ID, db_stat_id(i), StatKind::GAUGE, "stress", "registered", "Concurrent registration");
            }
        }

        ~db_statistic_set()
        {
            for (uint32_t i = 0; i < DB_STAT_COUNT; i++)
            {
                statistics_[i]->~SystemStatisticType();
            }
        }
    };

    struct db_registration_shared
    {
        SystemStatisticsDB       *db_;
        db_statistic_set         *statistic_set_;
        minstd::atomic<bool>      go_{false};
        minstd::atomic<uint32_t>  registrars_done_{0};
        minstd::atomic<uint32_t>  registration_counts_[DB_STAT_COUNT];
        minstd::atomic<uint32_t>  duplicate_count_{0};
        minstd::atomic<uint32_t>  failure_count_{0};
    };

    struct db_registrar_arg
    {
        db_registration_shared *shared_;
        uint32_t                first_index_;
    };

    static void *db_registrar_worker(void *arg)
    {
        auto *worker_arg = static_cast<db_registrar_arg *>(arg);
        db_registration_shared *shared = worker_arg->shared_;

        while (!shared->go_.load(minstd::memory_order_acquire))
        {
        }

        //  Each registrar starts at a different index, so every statistic is contended by every registrar.

        for (uint32_t step = 0; step < DB_STAT_COUNT; step++)
        {
            const uint32_t stat_index = (worker_arg->first_index_ + step) % DB_STAT_COUNT;

            switch (shared->db_->Register(*shared->statistic_set_->statistics_[stat_index]))
            {
                case SystemStatisticsDB::StatsDBResultCodes::SUCCESS:
                    shared->registration_counts_[stat_index].fetch_add(1, minstd::memory_order_relaxed);
                    break;

                case SystemStatisticsDB::StatsDBResultCodes::DUPLICATE_ID:
                    shared->duplicate_count_.fetch_add(1, minstd::memory_order_relaxed);
                    break;

                default:
                    shared->failure_count_.fetch_add(1, minstd::memory_order_relaxed);
                    break;
            }
        }

        shared->registrars_done_.fetch_add(1, minstd::memory_order_release);
        return nullptr;
    }

    struct db_concurrent_reader_arg
    {
        db_registration_shared *shared_;
        uint32_t                pass_count_;
        int                     failed_;
    };

    //  Every pass sees a strictly ascending run of registered statistics, and every lookup finds either nothing or
    //      the right object.

    static void *db_concurrent_reader_worker(void *arg)
    {
        auto *worker_arg = static_cast<db_concurrent_reader_arg *>(arg);
        db_registration_shared *shared = worker_arg->shared_;

        while (shared->registrars_done_.load(minstd::memory_order_acquire) < DB_REGISTRARS)
        {
            int64_t  previous_id = -1;
            uint32_t visited_count = 0;

            shared->db_->ForEach([&](const SystemStatistic &stat)
            {
                const uint32_t id = stat.id();
                const uint32_t index = (id - 1) / DB_ID_STRIDE;

                if ((static_cast<int64_t>(id) <= previous_id) || (index >= DB_STAT_COUNT) || (&stat != shared->statistic_set_->statistics_[index]))
                {
                    worker_arg->failed_ = 1;
                }

                previous_id = id;
                visited_count++;

                return SystemStatisticsDB::VisitAction::CONTINUE;
            });

            if (visited_count > DB_STAT_COUNT)
            {
                worker_arg->failed_ = 1;
            }

            const uint32_t index = worker_arg->pass_count_ % DB_STAT_COUNT;
            const SystemStatistic *found = FoundStatistic(*shared->db_, db_stat_id(index));

            if ((found != nullptr) && (found != shared->statistic_set_->statistics_[index]))
            {
                worker_arg->failed_ = 1;
            }

            if (shared->db_->Contains(SystemStatistic::KERNEL_PROCESS_ID, db_stat_id(index) + 1))
            {
                worker_arg->failed_ = 1;
            }

            worker_arg->pass_count_++;
        }

        return nullptr;
    }

    struct db_schema_reader_arg
    {
        db_registration_shared *shared_;
        uint32_t                schema_count_;
        int                     failed_;
    };

    static void *db_schema_reader_worker(void *arg)
    {
        auto *worker_arg = static_cast<db_schema_reader_arg *>(arg);
        db_registration_shared *shared = worker_arg->shared_;

        while (shared->registrars_done_.load(minstd::memory_order_acquire) < DB_REGISTRARS)
        {
            auto schema_result = shared->db_->Schema();

            if (schema_result.Failed() || (schema_result->Count() > DB_STAT_COUNT))
            {
                worker_arg->failed_ = 1;
                break;
            }

            const SystemStatisticsSchema &schema = *schema_result.Value();

            for (uint32_t i = 0; i < schema.Count(); i++)
            {
                if ((i > 0) && (schema[i].id() <= schema[i - 1].id()))
                {
                    worker_arg->failed_ = 1;
                }
            }

            worker_arg->schema_count_++;
        }

        return nullptr;
    }

    struct db_same_id_arg
    {
        SystemStatisticsDB      *db_;
        const SystemStatistic   *stat_;
        minstd::atomic<bool>    *go_;
        SystemStatisticsDB::StatsDBResultCodes      result_;
    };

    static void *db_same_id_worker(void *arg)
    {
        auto *worker_arg = static_cast<db_same_id_arg *>(arg);

        while (!worker_arg->go_->load(minstd::memory_order_acquire))
        {
        }

        worker_arg->result_ = worker_arg->db_->Register(*worker_arg->stat_);
        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(DbStressTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(DbStressTests, ConcurrentReadsAgainstPrePopulatedDb)
    {
        SystemStatisticsDB db;
        CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Initialize(stats_db_test_resource)));
        CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Register(db_gauge)));
        CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Register(db_counter)));
        CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Register(db_timestamp)));

        pthread_t     threads[NUM_THREADS];
        db_reader_arg reader_args[NUM_THREADS];

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            reader_args[i] = {&db, OPS_PER_THREAD / 10, 0};
            pthread_create(&threads[i], nullptr, db_reader_worker, &reader_args[i]);
        }
        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], nullptr);
        }

        for (uint32_t i = 0; i < NUM_THREADS; i++)
        {
            CHECK_EQUAL(0, reader_args[i].failed_);
        }
    }

    TEST(DbStressTests, ConcurrentRegistrationRegistersEachStatisticExactlyOnce)
    {
        db_statistic_set set;

        for (uint32_t round = 0; round < DB_ROUNDS; round++)
        {
            SystemStatisticsDB db;
            CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Initialize(stats_db_test_resource)));

            db_registration_shared shared;
            shared.db_ = &db;
            shared.statistic_set_ = &set;

            pthread_t                registrars[DB_REGISTRARS];
            db_registrar_arg         registrar_args[DB_REGISTRARS];
            pthread_t                readers[DB_READERS];
            db_concurrent_reader_arg reader_args[DB_READERS];

            for (uint32_t i = 0; i < DB_READERS; i++)
            {
                reader_args[i] = {&shared, 0, 0};
                pthread_create(&readers[i], nullptr, db_concurrent_reader_worker, &reader_args[i]);
            }
            for (uint32_t i = 0; i < DB_REGISTRARS; i++)
            {
                registrar_args[i] = {&shared, i * (DB_STAT_COUNT / DB_REGISTRARS)};
                pthread_create(&registrars[i], nullptr, db_registrar_worker, &registrar_args[i]);
            }

            shared.go_.store(true, minstd::memory_order_release);

            for (uint32_t i = 0; i < DB_REGISTRARS; i++)
            {
                pthread_join(registrars[i], nullptr);
            }
            for (uint32_t i = 0; i < DB_READERS; i++)
            {
                pthread_join(readers[i], nullptr);
                CHECK_EQUAL(0, reader_args[i].failed_);
            }

            for (uint32_t i = 0; i < DB_STAT_COUNT; i++)
            {
                CHECK_EQUAL(1u, shared.registration_counts_[i].load());
                POINTERS_EQUAL(set.statistics_[i], FoundStatistic(db, db_stat_id(i)));
            }

            CHECK_EQUAL(DB_STAT_COUNT * (DB_REGISTRARS - 1), shared.duplicate_count_.load());
            CHECK_EQUAL(0u, shared.failure_count_.load());
            CHECK_EQUAL(DB_STAT_COUNT, db.Size());

            uint32_t expected_index = 0;
            db.ForEach([&](const SystemStatistic &stat)
            {
                POINTERS_EQUAL(set.statistics_[expected_index], &stat);
                expected_index++;

                return SystemStatisticsDB::VisitAction::CONTINUE;
            });
            CHECK_EQUAL(DB_STAT_COUNT, expected_index);
        }
    }

    TEST(DbStressTests, SchemaDuringRegistrationIsAConsistentSnapshot)
    {
        db_statistic_set set;

        for (uint32_t round = 0; round < DB_ROUNDS; round++)
        {
            SystemStatisticsDB db;
            CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Initialize(stats_db_test_resource)));

            db_registration_shared shared;
            shared.db_ = &db;
            shared.statistic_set_ = &set;

            pthread_t            registrars[DB_REGISTRARS];
            db_registrar_arg     registrar_args[DB_REGISTRARS];
            pthread_t            schema_reader;
            db_schema_reader_arg schema_arg = {&shared, 0, 0};

            pthread_create(&schema_reader, nullptr, db_schema_reader_worker, &schema_arg);
            for (uint32_t i = 0; i < DB_REGISTRARS; i++)
            {
                registrar_args[i] = {&shared, i * (DB_STAT_COUNT / DB_REGISTRARS)};
                pthread_create(&registrars[i], nullptr, db_registrar_worker, &registrar_args[i]);
            }

            shared.go_.store(true, minstd::memory_order_release);

            for (uint32_t i = 0; i < DB_REGISTRARS; i++)
            {
                pthread_join(registrars[i], nullptr);
            }
            pthread_join(schema_reader, nullptr);

            CHECK_EQUAL(0, schema_arg.failed_);

            auto schema_result = db.Schema();
            CHECK_TRUE(schema_result.Successful());
            const SystemStatisticsSchema &schema = *schema_result.Value();
            CHECK_EQUAL(DB_STAT_COUNT, schema.Count());
        }
    }

    TEST(DbStressTests, ConcurrentRegistrationOfOneIdHasExactlyOneWinner)
    {
        static constexpr uint32_t ROUNDS = 200;

        SystemStatisticType first(SystemStatistic::KERNEL_PROCESS_ID, 77, StatKind::GAUGE, "stress", "first", "Same id, first object");
        SystemStatisticType second(SystemStatistic::KERNEL_PROCESS_ID, 77, StatKind::GAUGE, "stress", "second", "Same id, second object");

        for (uint32_t round = 0; round < ROUNDS; round++)
        {
            SystemStatisticsDB db;
            CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::SUCCESS), static_cast<int>(db.Initialize(stats_db_test_resource)));

            minstd::atomic<bool> start{false};
            db_same_id_arg args[2] = {{&db, &first, &start, SystemStatisticsDB::StatsDBResultCodes::FAILURE},
                                      {&db, &second, &start, SystemStatisticsDB::StatsDBResultCodes::FAILURE}};
            pthread_t threads[2];

            pthread_create(&threads[0], nullptr, db_same_id_worker, &args[0]);
            pthread_create(&threads[1], nullptr, db_same_id_worker, &args[1]);
            start.store(true, minstd::memory_order_release);
            pthread_join(threads[0], nullptr);
            pthread_join(threads[1], nullptr);

            const bool first_won = (args[0].result_ == SystemStatisticsDB::StatsDBResultCodes::SUCCESS);
            const bool second_won = (args[1].result_ == SystemStatisticsDB::StatsDBResultCodes::SUCCESS);

            CHECK_TRUE(first_won != second_won);
            CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::DUPLICATE_ID), static_cast<int>(args[first_won ? 1 : 0].result_));
            POINTERS_EQUAL(first_won ? &first : &second, FoundStatistic(db, 77));
            CHECK_EQUAL(1u, db.Size());
        }
    }
}
