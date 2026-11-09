// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string.h>

#include <atomic>
#include <functional>
#include <lockfree/skiplist>

#include "os_config.h"
#include "result.h"
#include "services/stats_db/system_stats.h"
#include "heaps.h"

namespace system_stats
{
    class SystemStatisticsSchema
    {
    public:
        ~SystemStatisticsSchema();

        SystemStatisticsSchema(const SystemStatisticsSchema&) = delete;
        SystemStatisticsSchema& operator=(const SystemStatisticsSchema&) = delete;

        uint32_t                    Count()                    const { return entry_count_; }
        const SystemStatisticType&  operator[](uint32_t index) const { return entries_[index]; }

    private:
        friend class SystemStatisticsDB;

        //  Allocated() is false if the allocation failed.

        explicit SystemStatisticsSchema(uint32_t entry_capacity);

        bool Allocated() const { return (entry_capacity_ == 0) || (entries_ != nullptr); }

        //  Copies stat's metadata into the next entry; false when full.

        bool Append(const SystemStatistic &stat);

        uint32_t             entry_capacity_;
        uint32_t             entry_count_ = 0;
        SystemStatisticType* entries_;
    };

    //  Lock-free database of registered statistics, keyed by (process id, id).  Every operation may run on any core
    //      concurrently, and the readers from ISRs.  The capacity, fixed by Initialize(), is shared by compile-time
    //      and dynamic statistics.
    //
    //  The database holds pointers, not copies, reachable only inside Visit() or ForEach().  Compile-time statistics
    //      are added with Register() and never removed.  Dynamic statistics are added with RegisterDynamic() and
    //      removed with RemoveDynamic(); a removed one may be freed once ReadersQuiescent() returns true.
    //      RegisteredStatistic (registered_statistic.h) does this for kernel services.

    class SystemStatisticsDB
    {
    private:

        struct StatsDBTag {};

        //  8 levels and 1024-slot blocks suit a few thousand statistics.  The tag gives the map its own slot tables.

        using StatisticMap = minstd::skip_list<uint64_t,
                                               const SystemStatistic *,
                                               MAX_CORES,
                                               8,
                                               10,
                                               minstd::skiplist_extensions::null_skiplist_statistics,
                                               StatsDBTag>;

        static constexpr uint32_t MAX_REGISTER_ATTEMPTS = 4;

        typedef enum class InitializationState : uint32_t
        {
            UNINITIALIZED = 0,
            INITIALIZING,
            READY
        } InitializationState;

        //  The fence pairs with the one in ReadersQuiescent(): either the reader sees a removal or the quiescence
        //      check sees the reader.

        class ReadSection
        {
        public:

            explicit ReadSection(const SystemStatisticsDB &db)
                : db_(db)
            {
                db_.active_readers_.fetch_add(1, minstd::memory_order_seq_cst);
                __atomic_thread_fence(__ATOMIC_SEQ_CST);
            }

            ~ReadSection()
            {
                db_.active_readers_.fetch_sub(1, minstd::memory_order_release);
            }

            ReadSection(const ReadSection &) = delete;
            ReadSection &operator=(const ReadSection &) = delete;

        private:

            const SystemStatisticsDB &db_;
        };

    public:
        static constexpr uint32_t DEFAULT_CAPACITY = 4096;
        static constexpr uint32_t MEMORY_PER_STATISTIC = 160;      //  upper bound: a node and its share of a slot block

        typedef enum class StatsDBResultCodes : uint32_t
        {
            SUCCESS = 0,
            FAILURE,

            ALREADY_INITIALIZED,
            NOT_INITIALIZED,
            UNABLE_TO_ALLOCATE_DATABASE,
            UNABLE_TO_ALLOCATE_MEMORY,

            DUPLICATE_ID,
            INVALID_PROCESS_ID,
            NOT_KERNEL_ADDRESS,
            NOT_FOUND,
            DATABASE_FULL,

            INVALID_USER_STATISTIC,
            KIND_NOT_REPRESENTED_BY_VIEW,
            ALREADY_REGISTERED
        } StatsDBResultCodes;

        typedef enum class VisitAction : uint32_t
        {
            CONTINUE = 0,
            STOP
        } VisitAction;

        using VisitCallback = minstd::function<void(const SystemStatistic &stat)>;
        using ForEachCallback = minstd::function<VisitAction(const SystemStatistic &stat)>;

        //  Room for count statistics, all or none; the destructor returns what was not used.  If not Granted(),
        //      RegisterDynamic() returns DATABASE_FULL.

        class Reservation
        {
        public:

            Reservation(SystemStatisticsDB &db, uint32_t count);
            ~Reservation();

            Reservation(const Reservation &) = delete;
            Reservation &operator=(const Reservation &) = delete;

            bool Granted() const
            {
                return granted_;
            }

            template<DynamicStatisticType T>
            StatsDBResultCodes RegisterDynamic(T &stat)
            {
                return db_.RegisterDynamicReserved(stat, remaining_);
            }

        private:

            SystemStatisticsDB      &db_;
            uint32_t                remaining_;
            bool                    granted_;
        };

        constexpr SystemStatisticsDB() noexcept = default;

        ~SystemStatisticsDB();

        SystemStatisticsDB(const SystemStatisticsDB &) = delete;
        SystemStatisticsDB &operator=(const SystemStatisticsDB &) = delete;

        StatsDBResultCodes Initialize(minstd::pmr::memory_resource &resource, uint32_t capacity = DEFAULT_CAPACITY);

        //  0 until Initialize() succeeds.

        uint32_t Capacity() const
        {
            return capacity_.load(minstd::memory_order_acquire);
        }

        uint32_t Available() const
        {
            const uint32_t capacity = Capacity();
            const uint32_t committed = committed_.load(minstd::memory_order_relaxed);

            return (committed < capacity) ? capacity - committed : 0;
        }

        //  Adds a kernel statistic that is never removed, so it must outlive the database.  A duplicate id is
        //      reported as DUPLICATE_ID even when the database is full.

        StatsDBResultCodes Register(const SystemStatistic &stat);

        //  stat must stay valid until it has been removed and ReadersQuiescent() has returned true.

        template<DynamicStatisticType T>
        StatsDBResultCodes RegisterDynamic(T &stat)
        {
            Reservation reservation(*this, 1);

            return reservation.RegisterDynamic(stat);
        }

        //  NOT_FOUND if stat is not in the database; FAILURE if it could not be removed and is still there.  Not for
        //      the same statistic from two cores at once.

        template<DynamicStatisticType T>
        StatsDBResultCodes RemoveDynamic(const T &stat)
        {
            return RemoveDynamicStatistic(stat);
        }

        //  True when no reader is running: a statistic removed before a true result can no longer be reached.

        bool ReadersQuiescent() const
        {
            __atomic_thread_fence(__ATOMIC_SEQ_CST);

            return active_readers_.load(minstd::memory_order_acquire) == 0;
        }

        bool Contains(uint32_t process_id, uint32_t id) const
        {
            return Visit(process_id, id, [](const SystemStatistic &) {});
        }

        //  False if absent.  The reference is valid only inside callback.

        bool Visit(uint32_t process_id, uint32_t id, VisitCallback callback) const;

        //  In (process id, id) order until callback returns STOP.  A statistic registered or removed concurrently
        //      may or may not be visited.  The reference is valid only inside callback.

        void ForEach(ForEachCallback callback) const;

        //  A snapshot of every registered statistic's metadata, in (process id, id) order.

        PointerResult<StatsDBResultCodes, SystemStatisticsSchema> Schema() const;

        uint32_t Size() const;

        bool Empty() const
        {
            return Size() == 0;
        }

    private:

        alignas(StatisticMap) uint8_t       map_storage_[sizeof(StatisticMap)]{};
        minstd::atomic<StatisticMap *>      map_{nullptr};
        minstd::atomic<uint32_t>            state_{static_cast<uint32_t>(InitializationState::UNINITIALIZED)};
        mutable minstd::atomic<uint32_t>    active_readers_{0};
        minstd::atomic<uint32_t>            capacity_{0};
        minstd::atomic<uint32_t>            committed_{0};             //  registered or reserved; never above capacity_

        static constexpr uint64_t Key(uint32_t process_id, uint32_t id)
        {
            return (static_cast<uint64_t>(process_id) << 32) | id;
        }

        //  Tells a duplicate from a transient insert failure.

        StatsDBResultCodes Insert(StatisticMap &map, const SystemStatistic &stat);

        //  All or none.

        bool ReserveCapacity(uint32_t count);
        void ReleaseCapacity(uint32_t count);

        StatsDBResultCodes RegisterDynamicReserved(const SystemStatistic &stat, uint32_t &reserved_count);
        StatsDBResultCodes RemoveDynamicStatistic(const SystemStatistic &stat);
    };

    SystemStatisticsDB &GetSystemStatisticsDB();

    typedef struct StaticRegistrationSummary
    {
        uint32_t registered_count_ = 0;
        uint32_t already_registered_count_ = 0;     //  this same statistic was already in the database
        uint32_t duplicate_count_ = 0;              //  a different statistic with the same id was registered first
        uint32_t failed_count_ = 0;                 //  any other error, DATABASE_FULL included
        uint32_t first_duplicate_id_ = 0;           //  valid when duplicate_count_ > 0
    } StaticRegistrationSummary;

    //  Closes static recording and registers every recorded statistic in db.  Run it after static initialisation and
    //      before other cores construct statistics.  Every statistic is attempted; the result is the first error.

    SystemStatisticsDB::StatsDBResultCodes RegisterStaticStatistics(SystemStatisticsDB &db, StaticRegistrationSummary &summary);
}
