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
    //  Schema for the system statistics, defining the structure and capacity of the entries.
    
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

        //  Allocates room for entry_capacity entries; Allocated() is false if that failed.

        explicit SystemStatisticsSchema(uint32_t entry_capacity);

        bool Allocated() const { return (entry_capacity_ == 0) || (entries_ != nullptr); }

        //  Copies stat's metadata into the next entry; false when full.

        bool Append(const SystemStatistic &stat);

        uint32_t             entry_capacity_;
        uint32_t             entry_count_ = 0;
        SystemStatisticType* entries_;
    };

    //  Lock-free database of registered statistics, keyed by process id and id.  Every operation may run
    //      concurrently on any core, and the readers from ISRs.
    //
    //  Initialize() builds the map from a memory resource -- the kernel's static heap -- with a fixed capacity
    //      shared by compile-time and dynamic statistics.  Registration past it returns DATABASE_FULL, so the map
    //      uses at most about Capacity() * MEMORY_PER_STATISTIC bytes, plus removed nodes not yet reclaimed.  Before
    //      Initialize() the database is empty and registration returns NOT_INITIALIZED.
    //
    //  The database holds pointers to kernel objects, not copies.  Every statistic, compile-time or dynamic, is
    //      reached only inside Visit() or ForEach().
    //
    //      Compile-time statistics are added with Register(), never removed, and must outlive the database.
    //
    //      Dynamic statistics are added with RegisterDynamic() and removed with RemoveDynamic().  A removed one may
    //          be freed once ReadersQuiescent() returns true.

    class SystemStatisticsDB
    {
    private:

        struct StatsDBTag {};

        //  Sized for a few thousand statistics: 8 levels (~128 byte nodes) and 1024-slot blocks instead of the
        //      defaults for large maps.  The tag gives the map its own static slot tables.

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

        //  Counts the reader in active_readers_ for its lifetime.  The fence orders the count before every read of the
        //      map: it pairs with the one in ReadersQuiescent(), so either the reader sees a removal or the quiescence check sees the reader.

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
        //  The kernel database's capacity, and an upper bound on the map memory per statistic: a 128-byte node and its share of a slot block.

        static constexpr uint32_t DEFAULT_CAPACITY = 4096;
        static constexpr uint32_t MEMORY_PER_STATISTIC = 160;

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

            INVALID_USER_STATISTIC          //  a user image's (EL0) statistics section is malformed
        } StatsDBResultCodes;

        //  Returned by a ForEach() callback to keep walking or stop.

        typedef enum class VisitAction : uint32_t
        {
            CONTINUE = 0,
            STOP
        } VisitAction;

        using VisitCallback = minstd::function<void(const SystemStatistic &stat)>;
        using ForEachCallback = minstd::function<VisitAction(const SystemStatistic &stat)>;

        //  Holds room for statistics until RegisterDynamic() uses it; the destructor returns what is left.  Room
        //      is taken for all of them or none: Granted() is false if there was not room for all of them, and
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

            StatsDBResultCodes RegisterDynamic(DynamicStatistic &stat);

        private:

            SystemStatisticsDB      &db_;
            uint32_t                remaining_;
            bool                    granted_;
        };

        constexpr SystemStatisticsDB() noexcept = default;

        ~SystemStatisticsDB();

        SystemStatisticsDB(const SystemStatisticsDB &) = delete;
        SystemStatisticsDB &operator=(const SystemStatisticsDB &) = delete;

        //  Builds the map from resource with fixed capacity.

        StatsDBResultCodes Initialize(minstd::pmr::memory_resource &resource, uint32_t capacity = DEFAULT_CAPACITY);

        //  0 until Initialize() succeeds.

        uint32_t Capacity() const
        {
            return capacity_.load(minstd::memory_order_acquire);
        }

        //  Remaining space for additional statistics.

        uint32_t Available() const
        {
            const uint32_t capacity = Capacity();
            const uint32_t committed = committed_.load(minstd::memory_order_relaxed);

            return (committed < capacity) ? capacity - committed : 0;
        }

        //  Adds a compile-time statistic under process KERNEL_PROCESS_ID and stat.id().  Only the address is kept and
        //      it is never removed, so stat must outlive the database.  INVALID_PROCESS_ID if stat.ProcessId() is not
        //      KERNEL_PROCESS_ID; DUPLICATE_ID if the key is already registered, even when the database is full;
        //      DATABASE_FULL if there is no room.

        StatsDBResultCodes Register(const SystemStatistic &stat);

        //  Adds stat under stat.ProcessId() and stat.id().  'stat' must stay valid until it has been removed and
        //      ReadersQuiescent() has returned true.  DUPLICATE_ID if the key is already registered; DATABASE_FULL if
        //      there is no room.

        StatsDBResultCodes RegisterDynamic(DynamicStatistic &stat);

        //  Removes a statistic added by RegisterDynamic()
        //      Cannot be called for the same statistic from two cores at once.

        StatsDBResultCodes RemoveDynamic(const DynamicStatistic &stat);

        //  True when no Visit(), ForEach(), Contains() or Schema() is running.  A dynamic statistic
        //      removed before a call that returns true can no longer be reached by any reader.

        bool ReadersQuiescent() const
        {
            __atomic_thread_fence(__ATOMIC_SEQ_CST);

            return active_readers_.load(minstd::memory_order_acquire) == 0;
        }

        bool Contains(uint32_t process_id, uint32_t id) const
        {
            return Visit(process_id, id, [](const SystemStatistic &) {});
        }

        //  Calls callback for the statistic with this process id and id and returns true, or returns false if
        //      absent.  The reference is only valid inside callback.

        bool Visit(uint32_t process_id, uint32_t id, VisitCallback callback) const;

        //  Calls callback for each registered statistic in ascending (process id, id) order, until it returns
        //      VisitAction::STOP.  A statistic registered or removed concurrently may or may not be visited.
        //The reference is only valid inside callback.

        void ForEach(ForEachCallback callback) const;

        //  A snapshot of every registered statistic's metadata in ascending (process id, id) order.  NOT_INITIALIZED before
        //      Initialize(); UNABLE_TO_ALLOCATE_MEMORY if the snapshot cannot be allocated.

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

        //  Inserts stat under its key, telling a duplicate from a transient insert failure.

        StatsDBResultCodes Insert(StatisticMap &map, const SystemStatistic &stat);

        //  Takes room for count statistics, all or none.

        bool ReserveCapacity(uint32_t count);
        void ReleaseCapacity(uint32_t count);

        //  RegisterDynamic() taking its room from reserved_count, which is decremented on success.

        StatsDBResultCodes RegisterDynamicReserved(DynamicStatistic &stat, uint32_t &reserved_count);
    };

    //  The kernel's database.  InitializePlatform() initialises it and registers the static statistics.

    SystemStatisticsDB &GetSystemStatisticsDB();

    typedef struct StaticRegistrationSummary
    {
        uint32_t registered_count_ = 0;
        uint32_t already_registered_count_ = 0;     //  this same statistic was already in the database
        uint32_t duplicate_count_ = 0;              //  a different statistic with the same id was registered first
        uint32_t failed_count_ = 0;                 //  any other error, DATABASE_FULL included
        uint32_t first_duplicate_id_ = 0;           //  valid when duplicate_count_ > 0
    } StaticRegistrationSummary;

    //  Closes static recording, then registers every recorded statistic in db.  Run it once static initialisation
    //      is over and before other cores construct statistics; it may be run again to fill another database.
    //
    //  Every recorded statistic is attempted.  Returns SUCCESS if each one is now in db, or else the first error;
    //      summary reports the counts.

    SystemStatisticsDB::StatsDBResultCodes RegisterStaticStatistics(SystemStatisticsDB &db, StaticRegistrationSummary &summary);
}
