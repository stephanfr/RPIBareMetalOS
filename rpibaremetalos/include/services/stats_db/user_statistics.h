// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <atomic>
#include <memory_resource>

#include "result.h"
#include "services/stats_db/stats_db.h"
#include "services/stats_db/system_stats.h"
#include "uapi/user_statistics.h"

namespace system_stats
{
    //  The kernel's view of one statistic a user program defined (see uapi/user_statistics.h).  The kind, id
    //      and strings are copied when the image is loaded; the value is read through the kernel's own mapping of the
    //      process's page, so a read never depends on the installed address space and never faults.  Processes
    //      loaded from one binary share ids, group and names, and are told apart by process id.

    class UserStatistic : public DynamicStatistic
    {
    public:
        UserStatistic(const UserStatisticDescriptor &descriptor, int64_t *value, uint32_t process_id);

        UserStatistic(const UserStatistic &) = delete;
        UserStatistic &operator=(const UserStatistic &) = delete;

        uint32_t id() const noexcept override
        {
            return id_;
        }

        StatKind Kind() const noexcept override
        {
            return kind_;
        }

        const char *Group() const noexcept override
        {
            return group_;
        }

        const char *Name() const noexcept override
        {
            return name_;
        }

        const char *Description() const noexcept override
        {
            return description_;
        }

        const UserStatistic *AsUserStatistic() const noexcept override
        {
            return this;
        }

        bool NumericValue(int64_t &value) const noexcept override
        {
            value = Get();
            return true;
        }

        //  USER_STATISTIC_PRIVATE or USER_STATISTIC_PUBLIC, from the descriptor at load time.

        uint32_t Flags() const noexcept
        {
            return flags_;
        }

        bool Public() const noexcept
        {
            return (flags_ & USER_STATISTIC_PUBLIC) != 0;
        }

        //  The owning task's user-visible id (what it reads from tpidrro_el0).

        uint32_t ProcessId() const noexcept override
        {
            return process_id_;
        }

        int64_t Get() const noexcept
        {
            return __atomic_load_n(value_, __ATOMIC_RELAXED);
        }

        //  A kernel-side write, which the process sees at once: both mappings are of the same page.

        void SetValue(int64_t value) const noexcept
        {
            __atomic_store_n(value_, value, __ATOMIC_RELAXED);
        }

    private:
        uint32_t    id_;
        StatKind    kind_;
        uint32_t    flags_;
        uint32_t    process_id_;
        int64_t     *value_;                                         //  kernel VA, inside the region's pages
        char        group_[GROUP_MAX + 1];
        char        name_[NAME_MAX + 1];
        char        description_[DESCRIPTION_MAX + 1];
    };

    //  A reference to one UserStatistic that may be kept indefinitely.  It holds the process id and id, not the
    //      address: every call looks the statistic up again and fails once it has been removed, so a handle cannot
    //      dangle.

    class UserStatisticHandle
    {
    public:
        using VisitCallback = minstd::function<void(const UserStatistic &stat)>;

        //  Refers to nothing: every lookup fails.

        UserStatisticHandle() = default;

        //  The UserStatistic process process_id defined with id, in db.

        UserStatisticHandle(const SystemStatisticsDB &db, uint32_t process_id, uint32_t id)
            : db_(&db),
              process_id_(process_id),
              id_(id)
        {}

        //  For a statistic reached inside Visit() or ForEach() on db.

        UserStatisticHandle(const SystemStatisticsDB &db, const UserStatistic &stat)
            : UserStatisticHandle(db, stat.ProcessId(), stat.id())
        {}

        uint32_t ProcessId() const
        {
            return process_id_;
        }

        uint32_t Id() const
        {
            return id_;
        }

        //  The current value; NOT_FOUND once the statistic is gone.

        ValueResult<SystemStatisticsDB::StatsDBResultCodes, int64_t> Get() const
        {
            int64_t value = 0;

            if (!Visit([&value](const UserStatistic &stat) { value = stat.Get(); }))
            {
                return ValueResult<SystemStatisticsDB::StatsDBResultCodes, int64_t>::Failure(SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND);
            }

            return ValueResult<SystemStatisticsDB::StatsDBResultCodes, int64_t>::Success(value);
        }

        bool Exists() const
        {
            return Visit([](const UserStatistic &) {});
        }

        //  Calls callback and returns true, or returns false if the statistic is gone.  The reference is only valid
        //      inside callback.

        bool Visit(VisitCallback callback) const;

    private:
        const SystemStatisticsDB    *db_ = nullptr;
        uint32_t                    process_id_ = 0;
        uint32_t                    id_ = 0;
    };

    //  A handle to the statistic process process_id defined under group and name; NOT_FOUND if there is none.  O(n).

    ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticHandle> FindUserStatistic(const SystemStatisticsDB &db, uint32_t process_id, const char *group, const char *name);

    //  A process sees its own statistics and every public one, the kernel sees them all.

    class UserStatisticReader
    {
    public:
        static constexpr UserStatisticReader Kernel()
        {
            return UserStatisticReader(0, true);
        }

        //  process_id is the caller's user-visible task id.  0 owns nothing: it is never a user task's id.

        static constexpr UserStatisticReader Process(uint32_t process_id)
        {
            return UserStatisticReader(process_id, false);
        }

        uint32_t ProcessId() const noexcept
        {
            return process_id_;
        }

        bool CanRead(const UserStatistic &stat) const noexcept
        {
            return privileged_ || stat.Public() || ((process_id_ != 0) && (stat.ProcessId() == process_id_));
        }

    private:
        uint32_t    process_id_;
        bool        privileged_;

        constexpr UserStatisticReader(uint32_t process_id, bool privileged)
            : process_id_(process_id),
              privileged_(privileged)
        {}
    };

    using UserStatisticRecordResult = ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticRecord>;

    UserStatisticRecordResult ReadUserStatisticRecord(const SystemStatisticsDB &db,
                                                      const UserStatisticReader &reader,
                                                      uint32_t process_id,
                                                      uint32_t id);

    UserStatisticRecordResult FindUserStatisticRecord(const SystemStatisticsDB &db,
                                                      const UserStatisticReader &reader,
                                                      uint32_t process_id,
                                                      const char *group,
                                                      const char *name);

    //  The statistics of one service process: the pages its .stats section was copied into, and a UserStatistic
    //      per descriptor in them.  The loader creates it and the task releases it with RetireUserStatisticsRegion()
    //      when the process is torn down.

    class UserStatisticsRegion
    {
    public:
        //  Frees the pages the section was copied into, once nothing can read them.

        typedef void (*PageReleaseFunction)(void *pages, uint64_t size);

        //  Checks a copied .stats section: each USER_STATISTIC_DESCRIPTOR_SIZE slot is either padding (magic 0) or a
        //      well-formed descriptor with an id no other descriptor has.  Returns the number of descriptors, or
        //      INVALID_USER_STATISTIC.  The section must already be in kernel memory, out of the process's reach, so
        //      that what is checked is what is used.

        static ValueResult<SystemStatisticsDB::StatsDBResultCodes, uint32_t> Validate(const void *section, uint64_t size);

        //  Builds the region over a validated section of size bytes at pages (kernel VA) for process process_id,
        //      allocating the UserStatistic objects from resource.  INVALID_PROCESS_ID if process_id is the
        //      kernel's, INVALID_USER_STATISTIC if the section is malformed, UNABLE_TO_ALLOCATE_MEMORY if memory is
        //      short; the pages then still belong to the caller.

        static ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticsRegion *> Create(void *pages,
                                                                              uint64_t size,
                                                                              uint32_t process_id,
                                                                              PageReleaseFunction release_pages,
                                                                              minstd::pmr::memory_resource &resource);

        UserStatisticsRegion(const UserStatisticsRegion &) = delete;
        UserStatisticsRegion &operator=(const UserStatisticsRegion &) = delete;

        //  Registers every statistic in db: without room for all of them none is registered and the result is
        //      DATABASE_FULL, or NOT_INITIALIZED before Initialize().  Otherwise returns the first error, still
        //      attempting the rest.  Call it once.

        SystemStatisticsDB::StatsDBResultCodes Register(SystemStatisticsDB &db);

        uint32_t Count() const
        {
            return statistic_count_;
        }

        //  How many of the statistics Register() put in the database.

        uint32_t Registered() const
        {
            return registered_count_;
        }

        //  Regions created and not yet freed, across the system.

        static uint32_t LiveRegions()
        {
            return live_regions_.load(minstd::memory_order_relaxed);
        }

    private:

        friend SystemStatisticsDB::StatsDBResultCodes RetireUserStatisticsRegion(UserStatisticsRegion *region);
        friend uint32_t ReclaimRetiredUserStatisticsRegions();

        static inline constinit minstd::atomic<uint32_t> live_regions_{0};

        void                           *pages_;
        uint64_t                        pages_size_;
        PageReleaseFunction             release_pages_;
        minstd::pmr::memory_resource   &resource_;

        UserStatistic                  *statistics_ = nullptr;
        uint32_t                        statistic_count_ = 0;
        uint32_t                       *registered_flags_ = nullptr;    //  per statistic: 1 if in db_
        uint32_t                        registered_count_ = 0;
        SystemStatisticsDB             *db_ = nullptr;

        UserStatisticsRegion           *next_retired_ = nullptr;

        UserStatisticsRegion(void                           *pages,
                             uint64_t                       size,
                             PageReleaseFunction            release_pages,
                             minstd::pmr::memory_resource   &resource);

        ~UserStatisticsRegion();

        //  Destroys the statistics and the region and releases the pages.  Nothing may be able to reach them.

        void Destroy();
    };

    //  Removes the region's statistics from the database and frees the region once no reader can still be using
    //      them -- at once if possible, otherwise from a later ReclaimRetiredUserStatisticsRegions().  A statistic
    //      that cannot be removed keeps the whole region alive for good, rather than ever freeing memory a reader
    //      could reach; the result is then FAILURE.  region may be nullptr.

    SystemStatisticsDB::StatsDBResultCodes RetireUserStatisticsRegion(UserStatisticsRegion *region);

    //  Frees every retired region whose database has no reader that could still see its statistics.  Returns how
    //      many were freed.  Safe to call from any task, concurrently.

    uint32_t ReclaimRetiredUserStatisticsRegions();
}
