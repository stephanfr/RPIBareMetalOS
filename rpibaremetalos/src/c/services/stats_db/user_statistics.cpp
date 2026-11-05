// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/stats_db/user_statistics.h"

#include <string.h>

namespace system_stats
{
    static_assert(USER_STATISTIC_GAUGE == static_cast<uint32_t>(StatKind::GAUGE), "user and kernel kinds must match");
    static_assert(USER_STATISTIC_COUNTER == static_cast<uint32_t>(StatKind::COUNTER), "user and kernel kinds must match");
    static_assert(USER_STATISTIC_TIMESTAMP == static_cast<uint32_t>(StatKind::TIMESTAMP), "user and kernel kinds must match");

    static_assert(USER_STATISTIC_GROUP_LENGTH == SystemStatistic::GROUP_MAX + 1, "user and kernel string sizes must match");
    static_assert(USER_STATISTIC_NAME_LENGTH == SystemStatistic::NAME_MAX + 1, "user and kernel string sizes must match");
    static_assert(USER_STATISTIC_DESCRIPTION_LENGTH == SystemStatistic::DESCRIPTION_MAX + 1, "user and kernel string sizes must match");

    namespace
    {
        //  Retired regions waiting for their database's readers to drain.  Pushed by RetireUserStatisticsRegion(),
        //      taken whole by ReclaimRetiredUserStatisticsRegions().

        constinit minstd::atomic<UserStatisticsRegion *> retired_regions{nullptr};

        bool Terminated(const char *string, uint32_t length)
        {
            return strnlen(string, length) < length;
        }

        bool NumericKind(uint32_t kind)
        {
            return (kind == USER_STATISTIC_GAUGE) || (kind == USER_STATISTIC_COUNTER) || (kind == USER_STATISTIC_TIMESTAMP);
        }

        void CopyString(char *destination, const char *source, uint32_t length)
        {
            memcpy(destination, source, length);
            destination[length - 1] = '\0';
        }

        //  Everything is copied into a zeroed record, so no byte of it comes from anywhere but the statistic.

        void FillRecord(const UserStatistic &stat, UserStatisticRecord &record)
        {
            memset(&record, 0, sizeof(record));

            record.process_id_ = stat.ProcessId();
            record.id_ = stat.id();
            record.value_ = stat.Get();
            record.kind_ = static_cast<uint32_t>(stat.Kind());
            record.flags_ = stat.Flags();

            strlcpy(record.group_, stat.Group(), sizeof(record.group_));
            strlcpy(record.name_, stat.Name(), sizeof(record.name_));
            strlcpy(record.description_, stat.Description(), sizeof(record.description_));
        }

        UserStatisticRecordResult RecordOrNotFound(bool found, const UserStatisticRecord &record)
        {
            return found ? UserStatisticRecordResult::Success(record) : UserStatisticRecordResult::Failure(SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND);
        }
    }

    UserStatistic::UserStatistic(const UserStatisticDescriptor &descriptor, int64_t *value, uint32_t process_id)
        : id_(descriptor.id_),
          kind_(static_cast<StatKind>(descriptor.kind_)),
          flags_(descriptor.flags_),
          process_id_(process_id),
          value_(value)
    {
        CopyString(group_, descriptor.group_, sizeof(group_));
        CopyString(name_, descriptor.name_, sizeof(name_));
        CopyString(description_, descriptor.description_, sizeof(description_));
    }

    UserStatisticsRegion::UserStatisticsRegion(void *pages, uint64_t size, PageReleaseFunction release_pages, minstd::pmr::memory_resource &resource)
        : pages_(pages),
          pages_size_(size),
          release_pages_(release_pages),
          resource_(resource)
    {
        live_regions_.fetch_add(1, minstd::memory_order_relaxed);
    }

    UserStatisticsRegion::~UserStatisticsRegion()
    {
        for (uint32_t i = 0; i < statistic_count_; i++)
        {
            statistics_[i].~UserStatistic();
        }

        if (statistics_ != nullptr)
        {
            resource_.deallocate(statistics_, statistic_count_ * sizeof(UserStatistic), alignof(UserStatistic));
        }

        if (registered_flags_ != nullptr)
        {
            resource_.deallocate(registered_flags_, statistic_count_ * sizeof(uint32_t), alignof(uint32_t));
        }

        live_regions_.fetch_sub(1, minstd::memory_order_relaxed);
    }

    ValueResult<SystemStatisticsDB::StatsDBResultCodes, uint32_t> UserStatisticsRegion::Validate(const void *section, uint64_t size)
    {
        using Result = ValueResult<SystemStatisticsDB::StatsDBResultCodes, uint32_t>;

        if ((section == nullptr) && (size > 0))
        {
            return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::INVALID_USER_STATISTIC);
        }

        uint32_t statistic_count = 0;

        const uint8_t *section_bytes = static_cast<const uint8_t *>(section);

        //  The section is whole pages and descriptors do not divide a page, so a tail shorter than a descriptor
        //      is page padding.

        for (uint64_t offset = 0; offset + USER_STATISTIC_DESCRIPTOR_SIZE <= size; offset += USER_STATISTIC_DESCRIPTOR_SIZE)
        {
            const UserStatisticDescriptor *descriptor = reinterpret_cast<const UserStatisticDescriptor *>(section_bytes + offset);

            if (descriptor->magic_ == 0)
            {
                continue;
            }

            if ((descriptor->magic_ != USER_STATISTIC_MAGIC) ||
                !NumericKind(descriptor->kind_) ||
                ((descriptor->flags_ & ~USER_STATISTIC_KNOWN_FLAGS) != 0) ||
                !Terminated(descriptor->group_, USER_STATISTIC_GROUP_LENGTH) ||
                !Terminated(descriptor->name_, USER_STATISTIC_NAME_LENGTH) ||
                !Terminated(descriptor->description_, USER_STATISTIC_DESCRIPTION_LENGTH))
            {
                return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::INVALID_USER_STATISTIC);
            }

            //  The id is the statistic's key within its process: no earlier descriptor may have it.

            for (uint64_t earlier_offset = 0; earlier_offset < offset; earlier_offset += USER_STATISTIC_DESCRIPTOR_SIZE)
            {
                const UserStatisticDescriptor *earlier = reinterpret_cast<const UserStatisticDescriptor *>(section_bytes + earlier_offset);

                if ((earlier->magic_ != 0) && (earlier->id_ == descriptor->id_))
                {
                    return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::INVALID_USER_STATISTIC);
                }
            }

            statistic_count++;
        }

        return Result::Success(statistic_count);
    }

    ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticsRegion *> UserStatisticsRegion::Create(void *pages,
                                                                                         uint64_t size,
                                                                                         uint32_t process_id,
                                                                                         PageReleaseFunction release_pages,
                                                                                         minstd::pmr::memory_resource &resource)
    {
        using Result = ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticsRegion *>;

        if (process_id == SystemStatistic::KERNEL_PROCESS_ID)
        {
            return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::INVALID_PROCESS_ID);
        }

        const auto validation = Validate(pages, size);

        if (validation.Failed())
        {
            return Result::Failure(validation.ResultCode());
        }

        const uint32_t statistic_count = validation.Value();

        void *region_buffer = resource.allocate(sizeof(UserStatisticsRegion), alignof(UserStatisticsRegion));

        if (region_buffer == nullptr)
        {
            return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::UNABLE_TO_ALLOCATE_MEMORY);
        }

        UserStatisticsRegion *region = new (region_buffer) UserStatisticsRegion(pages, size, release_pages, resource);

        if (statistic_count > 0)
        {
            region->statistics_ = static_cast<UserStatistic *>(resource.allocate(statistic_count * sizeof(UserStatistic), alignof(UserStatistic)));
            region->registered_flags_ = static_cast<uint32_t *>(resource.allocate(statistic_count * sizeof(uint32_t), alignof(uint32_t)));

            //  statistic_count_ is still 0, so the destructor cannot size these arrays: free them here.

            if ((region->statistics_ == nullptr) || (region->registered_flags_ == nullptr))
            {
                if (region->statistics_ != nullptr)
                {
                    resource.deallocate(region->statistics_, statistic_count * sizeof(UserStatistic), alignof(UserStatistic));
                    region->statistics_ = nullptr;
                }

                if (region->registered_flags_ != nullptr)
                {
                    resource.deallocate(region->registered_flags_, statistic_count * sizeof(uint32_t), alignof(uint32_t));
                    region->registered_flags_ = nullptr;
                }

                region->~UserStatisticsRegion();
                resource.deallocate(region_buffer, sizeof(UserStatisticsRegion), alignof(UserStatisticsRegion));

                return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::UNABLE_TO_ALLOCATE_MEMORY);
            }

            uint8_t *section_bytes = static_cast<uint8_t *>(pages);
            uint32_t statistic_index = 0;

            for (uint64_t offset = 0; offset + USER_STATISTIC_DESCRIPTOR_SIZE <= size; offset += USER_STATISTIC_DESCRIPTOR_SIZE)
            {
                UserStatisticDescriptor *descriptor = reinterpret_cast<UserStatisticDescriptor *>(section_bytes + offset);

                if (descriptor->magic_ == 0)
                {
                    continue;
                }

                new (&region->statistics_[statistic_index]) UserStatistic(*descriptor, &descriptor->value_, process_id);
                region->registered_flags_[statistic_index] = 0;
                statistic_index++;
            }

            region->statistic_count_ = statistic_count;
        }

        return Result::Success(region);
    }

    SystemStatisticsDB::StatsDBResultCodes UserStatisticsRegion::Register(SystemStatisticsDB &db)
    {
        db_ = &db;

        //  Room for every statistic first, so a full database never leaves a process with only some of them.

        SystemStatisticsDB::Reservation reservation(db, statistic_count_);

        if (!reservation.Granted())
        {
            return (db.Capacity() == 0) ? SystemStatisticsDB::StatsDBResultCodes::NOT_INITIALIZED : SystemStatisticsDB::StatsDBResultCodes::DATABASE_FULL;
        }

        SystemStatisticsDB::StatsDBResultCodes first_error = SystemStatisticsDB::StatsDBResultCodes::SUCCESS;

        for (uint32_t i = 0; i < statistic_count_; i++)
        {
            const SystemStatisticsDB::StatsDBResultCodes result = reservation.RegisterDynamic(statistics_[i]);

            if (result == SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
            {
                registered_flags_[i] = 1;
                registered_count_++;
            }
            else if (first_error == SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
            {
                first_error = result;
            }
        }

        return first_error;
    }

    void UserStatisticsRegion::Destroy()
    {
        void *pages = pages_;
        const uint64_t size = pages_size_;
        const PageReleaseFunction release_pages = release_pages_;
        minstd::pmr::memory_resource &resource = resource_;

        this->~UserStatisticsRegion();
        resource.deallocate(this, sizeof(UserStatisticsRegion), alignof(UserStatisticsRegion));

        if (release_pages != nullptr)
        {
            release_pages(pages, size);
        }
    }

    bool UserStatisticHandle::Visit(VisitCallback callback) const
    {
        if (db_ == nullptr)
        {
            return false;
        }

        bool matched = false;

        db_->Visit(process_id_, id_, [&](const SystemStatistic &stat)
        {
            const UserStatistic *user_statistic = stat.AsUserStatistic();

            if (user_statistic != nullptr)
            {
                callback(*user_statistic);
                matched = true;
            }
        });

        return matched;
    }

    ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticHandle> FindUserStatistic(const SystemStatisticsDB &db, uint32_t process_id, const char *group, const char *name)
    {
        using Result = ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticHandle>;

        UserStatisticHandle found;
        bool matched = false;

        if ((group == nullptr) || (name == nullptr))
        {
            return Result::Failure(SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND);
        }

        db.ForEach([&](const SystemStatistic &stat)
        {
            const UserStatistic *user_statistic = stat.AsUserStatistic();

            if ((user_statistic == nullptr) ||
                (user_statistic->ProcessId() != process_id) ||
                (strncmp(stat.Group(), group, SystemStatistic::GROUP_MAX + 1) != 0) ||
                (strncmp(stat.Name(), name, SystemStatistic::NAME_MAX + 1) != 0))
            {
                return SystemStatisticsDB::VisitAction::CONTINUE;
            }

            found = UserStatisticHandle(db, *user_statistic);
            matched = true;
            return SystemStatisticsDB::VisitAction::STOP;
        });

        return matched ? Result::Success(found) : Result::Failure(SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND);
    }

    UserStatisticRecordResult ReadUserStatisticRecord(const SystemStatisticsDB &db,
                                                      const UserStatisticReader &reader,
                                                      uint32_t process_id,
                                                      uint32_t id)
    {
        UserStatisticRecord record;
        bool visible = false;

        UserStatisticHandle(db, process_id, id).Visit([&](const UserStatistic &stat)
        {
            if (reader.CanRead(stat))
            {
                FillRecord(stat, record);
                visible = true;
            }
        });

        return RecordOrNotFound(visible, record);
    }

    UserStatisticRecordResult FindUserStatisticRecord(const SystemStatisticsDB &db,
                                                      const UserStatisticReader &reader,
                                                      uint32_t process_id,
                                                      const char *group,
                                                      const char *name)
    {
        if ((group == nullptr) || (name == nullptr))
        {
            return UserStatisticRecordResult::Failure(SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND);
        }

        UserStatisticRecord record;
        bool found = false;

        db.ForEach([&](const SystemStatistic &stat)
        {
            const UserStatistic *user_statistic = stat.AsUserStatistic();

            if ((user_statistic == nullptr) ||
                (user_statistic->ProcessId() != process_id) ||
                !reader.CanRead(*user_statistic) ||
                (strncmp(stat.Group(), group, SystemStatistic::GROUP_MAX + 1) != 0) ||
                (strncmp(stat.Name(), name, SystemStatistic::NAME_MAX + 1) != 0))
            {
                return SystemStatisticsDB::VisitAction::CONTINUE;
            }

            FillRecord(*user_statistic, record);
            found = true;
            return SystemStatisticsDB::VisitAction::STOP;
        });

        return RecordOrNotFound(found, record);
    }

    SystemStatisticsDB::StatsDBResultCodes RetireUserStatisticsRegion(UserStatisticsRegion *region)
    {
        if (region == nullptr)
        {
            return SystemStatisticsDB::StatsDBResultCodes::SUCCESS;
        }

        bool all_removed = true;

        for (uint32_t i = 0; i < region->statistic_count_; i++)
        {
            if (region->registered_flags_[i] == 0)
            {
                continue;
            }

            const SystemStatisticsDB::StatsDBResultCodes result = region->db_->RemoveDynamic(region->statistics_[i]);

            if ((result == SystemStatisticsDB::StatsDBResultCodes::SUCCESS) || (result == SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND))
            {
                region->registered_flags_[i] = 0;
                region->registered_count_--;
            }
            else
            {
                all_removed = false;
            }
        }

        //  A statistic still in the database can be read at any time: never free it.

        if (!all_removed)
        {
            return SystemStatisticsDB::StatsDBResultCodes::FAILURE;
        }

        //  Every removal above happens before the push, which the reclaimer's exchange acquires.

        UserStatisticsRegion *head = retired_regions.load(minstd::memory_order_relaxed);

        do
        {
            region->next_retired_ = head;
        } while (!retired_regions.compare_exchange_weak(head, region, minstd::memory_order_release, minstd::memory_order_relaxed));

        ReclaimRetiredUserStatisticsRegions();

        return SystemStatisticsDB::StatsDBResultCodes::SUCCESS;
    }

    uint32_t ReclaimRetiredUserStatisticsRegions()
    {
        UserStatisticsRegion *retired_region = retired_regions.exchange(nullptr, minstd::memory_order_acquire);

        UserStatisticsRegion *kept_head = nullptr;
        UserStatisticsRegion *kept_tail = nullptr;
        uint32_t freed_count = 0;

        while (retired_region != nullptr)
        {
            UserStatisticsRegion *next_region = retired_region->next_retired_;

            //  A region never registered was never visible to a reader.

            if ((retired_region->db_ == nullptr) || retired_region->db_->ReadersQuiescent())
            {
                retired_region->Destroy();
                freed_count++;
            }
            else
            {
                retired_region->next_retired_ = kept_head;
                kept_head = retired_region;

                if (kept_tail == nullptr)
                {
                    kept_tail = retired_region;
                }
            }

            retired_region = next_region;
        }

        //  Put back what still has readers, ahead of anything retired meanwhile.

        if (kept_head != nullptr)
        {
            UserStatisticsRegion *head = retired_regions.load(minstd::memory_order_relaxed);

            do
            {
                kept_tail->next_retired_ = head;
            } while (!retired_regions.compare_exchange_weak(head, kept_head, minstd::memory_order_release, minstd::memory_order_relaxed));
        }

        return freed_count;
    }
}
