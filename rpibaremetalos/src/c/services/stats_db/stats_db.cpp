// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/stats_db/stats_db.h"

#if defined(__aarch64__)
#include "platform/address_space_layout.h"
#endif

namespace system_stats
{
    namespace
    {
        //  Constant-initialised, so it exists before any static constructor runs.

        constinit SystemStatisticsDB __system_statistics_db;

        //  The database dereferences what it holds from any core, ISR and address space, so it only takes objects in
        //      kernel memory: a user-space pointer means something different under every TTBR0.  The host-side unit
        //      tests have no kernel address range.

        bool InKernelMemory(const SystemStatistic &stat)
        {
#if defined(__aarch64__)
            return reinterpret_cast<uintptr_t>(&stat) >= KERNEL_VA_BASE;
#else
            (void)stat;
            return true;
#endif
        }
    }

    SystemStatisticsDB &GetSystemStatisticsDB()
    {
        return __system_statistics_db;
    }

    //  Reservation inner class first
    
    SystemStatisticsDB::Reservation::Reservation(SystemStatisticsDB &db, uint32_t count)
        : db_(db),
          remaining_(0),
          granted_(db.ReserveCapacity(count))
    {
        if (granted_)
        {
            remaining_ = count;
        }
    }

    SystemStatisticsDB::Reservation::~Reservation()
    {
        db_.ReleaseCapacity(remaining_);
    }

    //  Next, the stats db class itself

    SystemStatisticsDB::~SystemStatisticsDB()
    {
        StatisticMap *map = map_.load(minstd::memory_order_acquire);

        if (map != nullptr)
        {
            map->~StatisticMap();
        }
    }

    SystemStatisticsDB::StatsDBResultCodes SystemStatisticsDB::Initialize(minstd::pmr::memory_resource &resource, uint32_t capacity)
    {
        uint32_t expected = static_cast<uint32_t>(InitializationState::UNINITIALIZED);

        if (!state_.compare_exchange_strong(expected, static_cast<uint32_t>(InitializationState::INITIALIZING), minstd::memory_order_acq_rel, minstd::memory_order_relaxed))
        {
            return StatsDBResultCodes::ALREADY_INITIALIZED;
        }

        StatisticMap *map = new (map_storage_) StatisticMap(&resource);

        //  A map without sentinels is permanently empty: never publish it, and let a later call try again.

        if (!map->valid())
        {
            map->~StatisticMap();
            state_.store(static_cast<uint32_t>(InitializationState::UNINITIALIZED), minstd::memory_order_release);

            return StatsDBResultCodes::UNABLE_TO_ALLOCATE_DATABASE;
        }

        //  A reader that sees the map pointer sees the constructed map and the capacity.

        capacity_.store(capacity, minstd::memory_order_release);
        map_.store(map, minstd::memory_order_release);
        state_.store(static_cast<uint32_t>(InitializationState::READY), minstd::memory_order_release);

        return StatsDBResultCodes::SUCCESS;
    }

    SystemStatisticsDB::StatsDBResultCodes SystemStatisticsDB::Register(const SystemStatistic &stat)
    {
        StatisticMap *map = map_.load(minstd::memory_order_acquire);

        if (map == nullptr)
        {
            return StatsDBResultCodes::NOT_INITIALIZED;
        }

        if (!InKernelMemory(stat))
        {
            return StatsDBResultCodes::NOT_KERNEL_ADDRESS;
        }

        //  A statistic whose kind is chosen at run time is checked here; the others are checked at compile time.

        if (RepresentationOf(stat.Kind()) != stat.Representation())
        {
            return StatsDBResultCodes::KIND_NOT_REPRESENTED_BY_VIEW;
        }

        //  Statistics of any other process are removable: they go through RegisterDynamic().

        if (stat.ProcessId() != SystemStatistic::KERNEL_PROCESS_ID)
        {
            return StatsDBResultCodes::INVALID_PROCESS_ID;
        }

        //  A duplicate is reported as one even when the database is full: RegisterStaticStatistics() relies on it.

        if (map->find(Key(stat.ProcessId(), stat.id())) != map->end())
        {
            return StatsDBResultCodes::DUPLICATE_ID;
        }

        if (!ReserveCapacity(1))
        {
            return StatsDBResultCodes::DATABASE_FULL;
        }

        const StatsDBResultCodes result = Insert(*map, stat);

        if (result != StatsDBResultCodes::SUCCESS)
        {
            ReleaseCapacity(1);
        }

        return result;
    }

    SystemStatisticsDB::StatsDBResultCodes SystemStatisticsDB::RegisterDynamicReserved(const SystemStatistic &stat, uint32_t &reserved_count)
    {
        StatisticMap *map = map_.load(minstd::memory_order_acquire);

        if (map == nullptr)
        {
            return StatsDBResultCodes::NOT_INITIALIZED;
        }

        if (!InKernelMemory(stat))
        {
            return StatsDBResultCodes::NOT_KERNEL_ADDRESS;
        }

        //  A statistic whose kind is chosen at run time is checked here; the others are checked at compile time.

        if (RepresentationOf(stat.Kind()) != stat.Representation())
        {
            return StatsDBResultCodes::KIND_NOT_REPRESENTED_BY_VIEW;
        }

        //  As in Register(), a duplicate is reported as one even when there is no room.

        if (map->find(Key(stat.ProcessId(), stat.id())) != map->end())
        {
            return StatsDBResultCodes::DUPLICATE_ID;
        }

        if (reserved_count == 0)
        {
            return StatsDBResultCodes::DATABASE_FULL;
        }

        const StatsDBResultCodes result = Insert(*map, stat);

        if (result == StatsDBResultCodes::SUCCESS)
        {
            reserved_count--;
        }

        return result;
    }

    SystemStatisticsDB::StatsDBResultCodes SystemStatisticsDB::RemoveDynamicStatistic(const SystemStatistic &stat)
    {
        StatisticMap *map = map_.load(minstd::memory_order_acquire);

        if (map == nullptr)
        {
            return StatsDBResultCodes::NOT_INITIALIZED;
        }

        const uint64_t key = Key(stat.ProcessId(), stat.id());

        auto itr = map->find(key);

        if ((itr == map->end()) || (itr->second != &stat))
        {
            return StatsDBResultCodes::NOT_FOUND;
        }

        //  As for insert(), remove() can fail transiently; a lookup tells that from the key being gone.

        for (uint32_t attempt = 0; attempt < MAX_REGISTER_ATTEMPTS; attempt++)
        {
            if (map->remove(key) || (map->find(key) == map->end()))
            {
                ReleaseCapacity(1);
                return StatsDBResultCodes::SUCCESS;
            }
        }

        return StatsDBResultCodes::FAILURE;
    }

    bool SystemStatisticsDB::Visit(uint32_t process_id, uint32_t id, VisitCallback callback) const
    {
        const StatisticMap *map = map_.load(minstd::memory_order_acquire);

        if (map == nullptr)
        {
            return false;
        }

        ReadSection section(*this);

        auto itr = map->find(Key(process_id, id));

        if (itr == map->end())
        {
            return false;
        }

        callback(*itr->second);

        return true;
    }

    void SystemStatisticsDB::ForEach(ForEachCallback callback) const
    {
        const StatisticMap *map = map_.load(minstd::memory_order_acquire);

        if (map == nullptr)
        {
            return;
        }

        ReadSection section(*this);

        for (auto itr = map->begin(); itr != map->end(); ++itr)
        {
            if (callback(*itr->second) == VisitAction::STOP)
            {
                break;
            }
        }
    }

    bool SystemStatisticsDB::ReserveCapacity(uint32_t count)
    {
        const uint32_t capacity = Capacity();
        uint32_t committed = committed_.load(minstd::memory_order_relaxed);

        do
        {
            if ((committed > capacity) || (count > capacity - committed))
            {
                return false;
            }
        } while (!committed_.compare_exchange_weak(committed, committed + count, minstd::memory_order_relaxed, minstd::memory_order_relaxed));

        return true;
    }

    void SystemStatisticsDB::ReleaseCapacity(uint32_t count)
    {
        if (count > 0)
        {
            committed_.fetch_sub(count, minstd::memory_order_relaxed);
        }
    }

    SystemStatisticsDB::StatsDBResultCodes SystemStatisticsDB::Insert(StatisticMap &map, const SystemStatistic &stat)
    {
        //  insert() returns false both for a duplicate key and for a transient failure (allocation, or its
        //      bounded CAS retries under contention), so a failed insert is told apart by looking the key up.

        const uint64_t key = Key(stat.ProcessId(), stat.id());

        for (uint32_t attempt = 0; attempt < MAX_REGISTER_ATTEMPTS; attempt++)
        {
            if (map.insert(key, &stat))
            {
                return StatsDBResultCodes::SUCCESS;
            }

            if (map.find(key) != map.end())
            {
                return StatsDBResultCodes::DUPLICATE_ID;
            }
        }

        return StatsDBResultCodes::FAILURE;
    }

    uint32_t SystemStatisticsDB::Size() const
    {
        const StatisticMap *map = map_.load(minstd::memory_order_acquire);

        return (map != nullptr) ? map->size() : 0;
    }

    //  Two passes -- count, then copy -- so the entries can be allocated exactly.  A statistic registered between
    //      the passes is simply left out once the entries are full.

    PointerResult<SystemStatisticsDB::StatsDBResultCodes, SystemStatisticsSchema> SystemStatisticsDB::Schema() const
    {
        using Result = PointerResult<StatsDBResultCodes, SystemStatisticsSchema>;

        if (map_.load(minstd::memory_order_acquire) == nullptr)
        {
            return Result::Failure(StatsDBResultCodes::NOT_INITIALIZED);
        }

        const uint32_t statistic_count = Size();

        void *schema_buffer = __os_dynamic_heap_resource.allocate(sizeof(SystemStatisticsSchema), alignof(SystemStatisticsSchema));

        if (schema_buffer == nullptr)
        {
            return Result::Failure(StatsDBResultCodes::UNABLE_TO_ALLOCATE_MEMORY);
        }

        minstd::unique_ptr<SystemStatisticsSchema> schema(new (schema_buffer) SystemStatisticsSchema(statistic_count), __os_dynamic_heap_resource);

        if (!schema->Allocated())
        {
            return Result::Failure(StatsDBResultCodes::UNABLE_TO_ALLOCATE_MEMORY);
        }

        SystemStatisticsSchema *raw_schema = schema.get();

        //  The foreach will exit if Append() returns false, indicating the schema is full.

        ForEach([raw_schema](const SystemStatistic &stat)
        {
            return raw_schema->Append(stat) ? VisitAction::CONTINUE : VisitAction::STOP;
        });

        return Result::Success(minstd::move(schema));
    }

    SystemStatisticsDB::StatsDBResultCodes RegisterStaticStatistics(SystemStatisticsDB &db, StaticRegistrationSummary &summary)
    {
        StaticStatisticLink::CloseStaticRecording();

        summary = StaticRegistrationSummary();

        SystemStatisticsDB::StatsDBResultCodes first_error = SystemStatisticsDB::StatsDBResultCodes::SUCCESS;

        for (const StaticStatisticLink *link = StaticStatisticLink::FirstStaticStatistic(); link != nullptr; link = link->NextStaticStatistic())
        {
            const SystemStatistic &stat = link->Statistic();

            SystemStatisticsDB::StatsDBResultCodes result = db.Register(stat);

            if (result == SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
            {
                summary.registered_count_++;
                continue;
            }

            if (result == SystemStatisticsDB::StatsDBResultCodes::DUPLICATE_ID)
            {
                //  db may already hold this very statistic (registered explicitly, or by an earlier call): not an error.

                bool same_statistic = false;

                db.Visit(stat.ProcessId(), stat.id(), [&](const SystemStatistic &registered) { same_statistic = (&registered == &stat); });

                if (same_statistic)
                {
                    summary.already_registered_count_++;
                    continue;
                }

                if (summary.duplicate_count_++ == 0)
                {
                    summary.first_duplicate_id_ = stat.id();
                }
            }
            else
            {
                summary.failed_count_++;
            }

            if (first_error == SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
            {
                first_error = result;
            }
        }

        return first_error;
    }

    SystemStatisticsSchema::SystemStatisticsSchema(uint32_t entry_capacity)
        : entry_capacity_(entry_capacity),
          entries_(entry_capacity > 0
              ? static_cast<SystemStatisticType*>(__os_dynamic_heap_resource.allocate(entry_capacity * sizeof(SystemStatisticType), alignof(SystemStatisticType)))
              : nullptr)
    {
    }

    SystemStatisticsSchema::~SystemStatisticsSchema()
    {
        for (uint32_t i = 0; i < entry_count_; i++)
        {
            entries_[i].~SystemStatisticType();
        }

        if (entries_ != nullptr)
        {
            __os_dynamic_heap_resource.deallocate(entries_, entry_capacity_ * sizeof(SystemStatisticType), alignof(SystemStatisticType));
        }
    }

    bool SystemStatisticsSchema::Append(const SystemStatistic &stat)
    {
        if ((entries_ == nullptr) || (entry_count_ >= entry_capacity_))
        {
            return false;
        }

        new (&entries_[entry_count_]) SystemStatisticType( stat.ProcessId(),
                                                           stat.id(),
                                                           stat.Kind(),
                                                           stat.Group(),
                                                           stat.Name(),
                                                           stat.Description());
        entry_count_++;

        return true;
    }
}
