// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Dynamic statistics in SystemStatisticsDB, and the kernel side of user-program statistics: validating a copied
//      .stats section, the UserStatistic view of it, and retiring a region only once no reader can reach it.

#include "cpputest_support.h"
#include "stats_db_test_support.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "services/stats_db/stats_db.h"
#include "services/stats_db/user_statistics.h"

namespace
{
    using namespace system_stats;

#define CHECK_DB_RESULT(expected, expression) CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::expected), static_cast<int>(expression))

    constexpr uint64_t SECTION_SIZE = 4096;
    constexpr uint32_t SECTION_SLOTS = SECTION_SIZE / USER_STATISTIC_DESCRIPTOR_SIZE;
    constexpr int64_t POISON = 0x5EADBEEF5EADBEEFLL;
    constexpr uint32_t TEST_PROCESS_ID = 42;

    //  A section as the loader has it: page-sized, zero-filled, in memory the kernel owns.

    void *AllocateSection()
    {
        void *section = stats_db_test_resource.allocate(SECTION_SIZE, 64);
        memset(section, 0, SECTION_SIZE);
        return section;
    }

    minstd::atomic<uint32_t> sections_released{0};

    //  Overwrites every value first, so a reader that reaches a released section reads POISON.

    void ReleaseSection(void *pages, uint64_t size)
    {
        UserStatisticDescriptor *descriptors = static_cast<UserStatisticDescriptor *>(pages);

        for (uint32_t i = 0; i < size / USER_STATISTIC_DESCRIPTOR_SIZE; i++)
        {
            __atomic_store_n(&descriptors[i].value_, POISON, __ATOMIC_RELAXED);
        }

        stats_db_test_resource.deallocate(pages, size, 64);
        sections_released.fetch_add(1, minstd::memory_order_relaxed);
    }

    UserStatisticDescriptor &Slot(void *section, uint32_t index)
    {
        return static_cast<UserStatisticDescriptor *>(section)[index];
    }

    //  The statistic's id is its slot plus one, so every slot's is different.

    void Describe(void *section, uint32_t index, uint32_t kind, const char *group, const char *name, const char *description, int64_t value)
    {
        UserStatisticDescriptor &descriptor = Slot(section, index);

        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.magic_ = USER_STATISTIC_MAGIC;
        descriptor.id_ = index + 1;
        descriptor.kind_ = kind;
        descriptor.value_ = value;
        strlcpy(descriptor.group_, group, sizeof(descriptor.group_));
        strlcpy(descriptor.name_, name, sizeof(descriptor.name_));
        strlcpy(descriptor.description_, description, sizeof(descriptor.description_));
    }

    //  The usual three: a gauge, a counter and a timestamp, at slots 0, 1 and 2.

    void *SectionWithThreeStatistics()
    {
        void *section = AllocateSection();

        Describe(section, 0, USER_STATISTIC_GAUGE, "fs", "open files", "Files open now", 7);
        Describe(section, 1, USER_STATISTIC_COUNTER, "fs", "reads", "Blocks read", 100);
        Describe(section, 2, USER_STATISTIC_TIMESTAMP, "fs", "last mount", "When last mounted", 0);

        return section;
    }

    //  Every statistic in these databases is a UserStatistic.

    int64_t ValueOf(const SystemStatistic &stat)
    {
        const auto value = stat.GetValue<int64_t>();

        CHECK_TRUE(value.Successful());
        return value.Successful() ? *value : 0;
    }

    class TestDynamicStatistic final : public ScalarStatisticView, public DynamicStatistic
    {
    public:
        explicit TestDynamicStatistic(uint32_t id, uint32_t process_id = TEST_PROCESS_ID, StatKind kind = StatKind::GAUGE)
            : id_(id),
              process_id_(process_id),
              kind_(kind)
        {
        }

        uint32_t id() const noexcept override { return id_; }
        uint32_t ProcessId() const noexcept override { return process_id_; }
        StatKind Kind() const noexcept override { return kind_; }
        const char *Group() const noexcept override { return "test"; }
        const char *Name() const noexcept override { return "dynamic"; }
        const char *Description() const noexcept override { return "A test dynamic statistic"; }
        int64_t Get() const noexcept override { return 0; }

    private:
        uint32_t id_;
        uint32_t process_id_;
        StatKind kind_;
    };

    //  Helpers for calls a test expects to succeed: each checks the result and returns its value.

    UserStatisticsRegion *CreateRegion(void *section, uint32_t process_id)
    {
        const auto created = UserStatisticsRegion::Create(section, SECTION_SIZE, process_id, ReleaseSection, stats_db_test_resource);

        CHECK_DB_RESULT(SUCCESS, created.ResultCode());
        return created.Successful() ? *created : nullptr;
    }

    uint32_t ExpectValidCount(const void *section, uint64_t size)
    {
        const auto validation = UserStatisticsRegion::Validate(section, size);

        CHECK_DB_RESULT(SUCCESS, validation.ResultCode());
        return validation.Successful() ? *validation : 0;
    }

    UserStatisticHandle ExpectHandle(const ValueResult<SystemStatisticsDB::StatsDBResultCodes, UserStatisticHandle> &found)
    {
        CHECK_DB_RESULT(SUCCESS, found.ResultCode());
        return found.Successful() ? *found : UserStatisticHandle();
    }

    int64_t ExpectValue(const UserStatisticHandle &handle)
    {
        const auto value = handle.Get();

        CHECK_DB_RESULT(SUCCESS, value.ResultCode());
        return value.Successful() ? *value : 0;
    }

    UserStatisticRecord ExpectRecord(const UserStatisticRecordResult &result)
    {
        CHECK_DB_RESULT(SUCCESS, result.ResultCode());
        return result.Successful() ? *result : UserStatisticRecord{};
    }

    // =========================================================================
    //  Dynamic statistics in the database
    // =========================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(DynamicStatisticTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        }
    };
#pragma GCC diagnostic pop

    TEST(DynamicStatisticTests, RegistrationKeepsTheStatisticsOwnKey)
    {
        TestDynamicStatistic first(1);
        TestDynamicStatistic second(2);
        TestDynamicStatistic other_process(1, TEST_PROCESS_ID + 1);

        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(first));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(second));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(other_process));

        CHECK_EQUAL(1u, first.id());
        CHECK_EQUAL(TEST_PROCESS_ID, first.ProcessId());
        POINTERS_EQUAL(&first, FoundStatistic(db, TEST_PROCESS_ID, 1));
        POINTERS_EQUAL(&second, FoundStatistic(db, TEST_PROCESS_ID, 2));
        POINTERS_EQUAL(&other_process, FoundStatistic(db, TEST_PROCESS_ID + 1, 1));
        CHECK_EQUAL(3u, db.Size());
    }

    TEST(DynamicStatisticTests, RegistrationRefusesAKindItsViewDoesNotRepresent)
    {
        TestDynamicStatistic text_kind(1, TEST_PROCESS_ID, StatKind::TEXT);
        TestDynamicStatistic array_kind(2, TEST_PROCESS_ID, StatKind::ARRAY);
        TestDynamicStatistic counter_kind(3, TEST_PROCESS_ID, StatKind::COUNTER);

        CHECK_DB_RESULT(KIND_NOT_REPRESENTED_BY_VIEW, db.RegisterDynamic(text_kind));

        SystemStatisticsDB::Reservation reservation(db, 2);

        CHECK_TRUE(reservation.Granted());
        CHECK_DB_RESULT(KIND_NOT_REPRESENTED_BY_VIEW, reservation.RegisterDynamic(array_kind));
        CHECK_DB_RESULT(SUCCESS, reservation.RegisterDynamic(counter_kind));     //  the refusal used none of the room

        CHECK_EQUAL(1u, db.Size());
        CHECK_FALSE(db.Contains(TEST_PROCESS_ID, 1));
        CHECK_FALSE(db.Contains(TEST_PROCESS_ID, 2));
    }

    TEST(DynamicStatisticTests, SameIdInDifferentProcessesAreDistinct)
    {
        TestScalarStatistic kernel(SystemStatistic::KERNEL_PROCESS_ID, 7, StatKind::GAUGE, "g", "kernel", "desc");
        TestDynamicStatistic first_process(7, 1);
        TestDynamicStatistic second_process(7, 2);

        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(second_process));
        CHECK_DB_RESULT(SUCCESS, db.Register(kernel));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(first_process));

        CHECK_EQUAL(3u, db.Size());
        POINTERS_EQUAL(&kernel, FoundStatistic(db, 7));
        POINTERS_EQUAL(&first_process, FoundStatistic(db, 1, 7));
        POINTERS_EQUAL(&second_process, FoundStatistic(db, 2, 7));

        uint32_t process_ids[3] = {};
        uint32_t count = 0;

        db.ForEach([&](const SystemStatistic &stat)
        {
            process_ids[count++] = stat.ProcessId();
            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(3u, count);
        CHECK_EQUAL(SystemStatistic::KERNEL_PROCESS_ID, process_ids[0]);    //  ordered by process id first
        CHECK_EQUAL(1u, process_ids[1]);
        CHECK_EQUAL(2u, process_ids[2]);

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(first_process));
        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(second_process));
    }

    TEST(DynamicStatisticTests, DynamicStatisticsAreReachedThroughVisitors)
    {
        TestDynamicStatistic stat(42);
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(stat));

        CHECK_TRUE(db.Contains(TEST_PROCESS_ID, 42));
        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 42));

        const SystemStatistic *visited = nullptr;
        CHECK_TRUE(db.Visit(TEST_PROCESS_ID, 42, [&visited](const SystemStatistic &s) { visited = &s; }));
        POINTERS_EQUAL(&stat, visited);
    }

    TEST(DynamicStatisticTests, RemovedStatisticIsGoneUntilRegisteredAgain)
    {
        TestDynamicStatistic stat(42);
        TestDynamicStatistic never_registered(43);
        TestDynamicStatistic same_key(42);

        CHECK_DB_RESULT(NOT_FOUND, db.RemoveDynamic(never_registered));

        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(stat));
        CHECK_DB_RESULT(DUPLICATE_ID, db.RegisterDynamic(stat));
        CHECK_DB_RESULT(DUPLICATE_ID, db.RegisterDynamic(same_key));
        CHECK_DB_RESULT(NOT_FOUND, db.RemoveDynamic(same_key));        //  only the registered object is removed
        CHECK_EQUAL(1u, db.Size());

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(stat));
        CHECK_FALSE(db.Contains(TEST_PROCESS_ID, 42));
        CHECK_EQUAL(0u, db.Size());

        CHECK_DB_RESULT(NOT_FOUND, db.RemoveDynamic(stat));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(stat));            //  the key is the same, so a reader still
        POINTERS_EQUAL(&stat, FoundStatistic(db, TEST_PROCESS_ID, 42)); //      holding it reaches the same object
    }

    TEST(DynamicStatisticTests, ReadersAreSeenUntilTheyFinish)
    {
        TestDynamicStatistic stat(1);
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(stat));

        CHECK_TRUE(db.ReadersQuiescent());

        bool quiescent_inside = true;

        db.ForEach([&](const SystemStatistic &) { quiescent_inside = db.ReadersQuiescent(); return SystemStatisticsDB::VisitAction::CONTINUE; });
        CHECK_FALSE(quiescent_inside);

        quiescent_inside = true;
        db.Visit(TEST_PROCESS_ID, stat.id(), [&](const SystemStatistic &) { quiescent_inside = db.ReadersQuiescent(); });
        CHECK_FALSE(quiescent_inside);

        quiescent_inside = true;
        db.ForEach([&](const SystemStatistic &)
        {
            db.Visit(TEST_PROCESS_ID, stat.id(), [](const SystemStatistic &) {});    //  nested sections count separately
            quiescent_inside = db.ReadersQuiescent();

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });
        CHECK_FALSE(quiescent_inside);

        CHECK_TRUE(db.ReadersQuiescent());
    }

    // =========================================================================
    //  Validating a copied .stats section
    // =========================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(UserStatisticsValidateTests, StatsDBTestFixture)
    {
        void *section = nullptr;

        void setup() override
        {
            StatsDBTestFixture::setup();
            section = AllocateSection();
        }

        void teardown() override
        {
            stats_db_test_resource.deallocate(section, SECTION_SIZE, 64);
        }
    };
#pragma GCC diagnostic pop

    TEST(UserStatisticsValidateTests, CountsDescriptorsAndSkipsPadding)
    {

        CHECK_EQUAL(0u, ExpectValidCount(section, SECTION_SIZE));    //  all padding

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        Describe(section, 5, USER_STATISTIC_COUNTER, "g", "b", "d", 0);
        Describe(section, SECTION_SLOTS - 1, USER_STATISTIC_TIMESTAMP, "g", "c", "d", 0);

        CHECK_EQUAL(3u, ExpectValidCount(section, SECTION_SIZE));

        CHECK_EQUAL(0u, ExpectValidCount(nullptr, 0));
    }

    TEST(UserStatisticsValidateTests, IgnoresATailShorterThanADescriptor)
    {

        //  A page holds 25 descriptors and 96 spare bytes; garbage there is page padding, not a descriptor.

        memset(static_cast<uint8_t *>(section) + SECTION_SLOTS * USER_STATISTIC_DESCRIPTOR_SIZE, 0xFF,
               SECTION_SIZE - SECTION_SLOTS * USER_STATISTIC_DESCRIPTOR_SIZE);

        CHECK_EQUAL(0u, ExpectValidCount(section, SECTION_SIZE));
    }

    TEST(UserStatisticsValidateTests, AcceptsBothVisibilities)
    {

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "private", "d", 0);
        Describe(section, 1, USER_STATISTIC_GAUGE, "g", "public", "d", 0);
        Slot(section, 1).flags_ = USER_STATISTIC_PUBLIC;

        CHECK_EQUAL(2u, ExpectValidCount(section, SECTION_SIZE));
    }

    TEST(UserStatisticsValidateTests, RejectsMalformedDescriptors)
    {

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        Slot(section, 0).magic_ = 0x12345678;
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, static_cast<uint32_t>(StatKind::TEXT), "g", "a", "d", 0);   //  not a numeric kind
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, 0, "g", "a", "d", 0);
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        memset(Slot(section, 0).group_, 'x', sizeof(Slot(section, 0).group_));           //  no terminator
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        memset(Slot(section, 0).name_, 'x', sizeof(Slot(section, 0).name_));
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        memset(Slot(section, 0).description_, 'x', sizeof(Slot(section, 0).description_));
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        Slot(section, 0).flags_ = 0x80;                                                  //  unknown flag
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        Describe(section, 0, USER_STATISTIC_GAUGE, "g", "a", "d", 0);
        Describe(section, 7, USER_STATISTIC_GAUGE, "g", "b", "d", 0);
        Slot(section, 7).id_ = Slot(section, 0).id_;                                     //  ids must be unique
        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Validate(section, SECTION_SIZE).ResultCode());

        CHECK_DB_RESULT(INVALID_USER_STATISTIC, UserStatisticsRegion::Create(section, SECTION_SIZE, TEST_PROCESS_ID, ReleaseSection, stats_db_test_resource).ResultCode());
    }

    // =========================================================================
    //  Regions: the kernel statistics over a section, and their retirement
    // =========================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(UserStatisticsRegionTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        }

        void teardown() override
        {
            ReclaimRetiredUserStatisticsRegions();
        }
    };
#pragma GCC diagnostic pop

    TEST(UserStatisticsRegionTests, StatisticsCopyMetadataAndShareTheValue)
    {
        void *section = SectionWithThreeStatistics();

        UserStatisticsRegion *region = CreateRegion(section, TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_EQUAL(3u, region->Count());

        CHECK_DB_RESULT(SUCCESS, region->Register(db));
        CHECK_EQUAL(3u, region->Registered());
        CHECK_EQUAL(3u, db.Size());

        //  The process changes its strings after loading: the kernel keeps the copies.

        strlcpy(Slot(section, 0).name_, "forged", sizeof(Slot(section, 0).name_));
        Slot(section, 0).kind_ = 99;

        uint32_t seen = 0;

        db.ForEach([&](const SystemStatistic &stat)
        {
            CHECK_EQUAL(seen + 1, stat.id());                     //  the descriptor's id
            CHECK_EQUAL(TEST_PROCESS_ID, stat.ProcessId());
            STRCMP_EQUAL("fs", stat.Group());

            if (seen == 0)
            {
                STRCMP_EQUAL("open files", stat.Name());
                STRCMP_EQUAL("Files open now", stat.Description());
                CHECK_EQUAL(static_cast<int>(StatKind::GAUGE), static_cast<int>(stat.Kind()));
                CHECK_EQUAL((int64_t)7, ValueOf(stat));

                //  The process's writes are what the kernel reads, and a kernel write is what the process reads.

                UserGaugeSet(&Slot(section, 0), 123);
                CHECK_EQUAL((int64_t)123, ValueOf(stat));

                static_cast<const UserStatistic &>(stat).SetValue(-1);
                CHECK_EQUAL((int64_t)-1, UserStatisticGet(&Slot(section, 0)));
            }
            else if (seen == 1)
            {
                STRCMP_EQUAL("reads", stat.Name());
                CHECK_EQUAL(static_cast<int>(StatKind::COUNTER), static_cast<int>(stat.Kind()));

                UserCounterIncrement(&Slot(section, 1), 5);
                CHECK_EQUAL((int64_t)105, ValueOf(stat));
            }
            else
            {
                STRCMP_EQUAL("last mount", stat.Name());
                CHECK_EQUAL(static_cast<int>(StatKind::TIMESTAMP), static_cast<int>(stat.Kind()));
                CHECK_EQUAL((int64_t)0, ValueOf(stat));
            }

            seen++;

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(3u, seen);

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
    }

    TEST(UserStatisticsRegionTests, StatisticsReportTheirValueThroughTheBaseClass)
    {
        void *section = SectionWithThreeStatistics();

        UserStatisticsRegion *region = CreateRegion(section, TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_DB_RESULT(SUCCESS, region->Register(db));

        UserCounterIncrement(&Slot(section, 1), 5);

        const int64_t expected[] = {7, 105, 0};
        uint32_t seen = 0;

        db.ForEach([&](const SystemStatistic &stat)
        {
            const auto value = stat.GetValue<int64_t>();

            CHECK_TRUE(value.Successful());
            CHECK_EQUAL(expected[seen], *value);

            seen++;

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(3u, seen);

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
    }

    TEST(UserStatisticsRegionTests, TwoProcessesOfOneBinaryAreToldApartByProcessId)
    {
        //  Two processes loaded from the same binary: same groups and names, different processes.

        UserStatisticsRegion *first = CreateRegion(SectionWithThreeStatistics(), 7);
        UserStatisticsRegion *second = CreateRegion(SectionWithThreeStatistics(), 8);
        CHECK(first != nullptr);
        CHECK(second != nullptr);

        CHECK_DB_RESULT(SUCCESS, first->Register(db));
        CHECK_DB_RESULT(SUCCESS, second->Register(db));
        CHECK_EQUAL(6u, db.Size());

        uint32_t open_files[2] = {};

        db.ForEach([&](const SystemStatistic &stat)
        {
            const UserStatistic &user_statistic = static_cast<const UserStatistic &>(stat);

            if (strncmp(stat.Name(), "open files", SystemStatistic::NAME_MAX + 1) == 0)
            {
                CHECK((user_statistic.ProcessId() == 7) || (user_statistic.ProcessId() == 8));
                open_files[user_statistic.ProcessId() - 7]++;
            }

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(1u, open_files[0]);
        CHECK_EQUAL(1u, open_files[1]);

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(first));
        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(second));
    }

    TEST(UserStatisticsRegionTests, RetiringRemovesTheStatisticsAndFreesTheRegion)
    {
        const uint32_t live_before = UserStatisticsRegion::LiveRegions();
        const uint32_t released_before = sections_released.load();

        UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_EQUAL(live_before + 1, UserStatisticsRegion::LiveRegions());

        CHECK_DB_RESULT(SUCCESS, region->Register(db));
        CHECK_EQUAL(3u, db.Size());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));

        CHECK_EQUAL(0u, db.Size());
        CHECK_EQUAL(live_before, UserStatisticsRegion::LiveRegions());     //  no reader: freed at once
        CHECK_EQUAL(released_before + 1, sections_released.load());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(nullptr));
    }

    TEST(UserStatisticsRegionTests, RegionIsFreedOnlyAfterReadersFinish)
    {
        const uint32_t live_before = UserStatisticsRegion::LiveRegions();

        UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_DB_RESULT(SUCCESS, region->Register(db));

        //  Retire from inside a reader, still holding a statistic: the region must outlive the reader.

        bool retired = false;

        db.ForEach([&](const SystemStatistic &stat)
        {
            if (!retired)
            {
                CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
                retired = true;

                CHECK_EQUAL(live_before + 1, UserStatisticsRegion::LiveRegions());
                CHECK_EQUAL(0u, ReclaimRetiredUserStatisticsRegions());
                CHECK_EQUAL((int64_t)7, ValueOf(stat));                   //  still readable
            }

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(0u, db.Size());
        CHECK_EQUAL(1u, ReclaimRetiredUserStatisticsRegions());
        CHECK_EQUAL(live_before, UserStatisticsRegion::LiveRegions());
    }

    TEST(UserStatisticsRegionTests, UnregisteredRegionIsFreedAtOnce)
    {
        const uint32_t live_before = UserStatisticsRegion::LiveRegions();

        //  Created but never registered -- as when mapping the section into the process failed.

        UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_EQUAL(0u, region->Registered());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
        CHECK_EQUAL(live_before, UserStatisticsRegion::LiveRegions());
    }

    TEST(UserStatisticsRegionTests, RegistrationInAnUninitializedDatabaseFailsCleanly)
    {
        SystemStatisticsDB uninitialized_db;

        UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), TEST_PROCESS_ID);
        CHECK(region != nullptr);

        CHECK_DB_RESULT(NOT_INITIALIZED, region->Register(uninitialized_db));
        CHECK_EQUAL(0u, region->Registered());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
    }

    TEST(UserStatisticsRegionTests, EmptySectionMakesAnEmptyRegion)
    {
        UserStatisticsRegion *region = CreateRegion(AllocateSection(), TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_EQUAL(0u, region->Count());

        CHECK_DB_RESULT(SUCCESS, region->Register(db));
        CHECK_EQUAL(0u, db.Size());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
    }

    // =========================================================================
    //  Handles: long-lived references that notice when a statistic goes
    // =========================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(UserStatisticHandleTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        }

        void teardown() override
        {
            ReclaimRetiredUserStatisticsRegions();
        }
    };
#pragma GCC diagnostic pop

    TEST(UserStatisticHandleTests, HandleReadsTheStatisticUntilItIsRemoved)
    {
        void *section = SectionWithThreeStatistics();

        UserStatisticsRegion *region = CreateRegion(section, TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_DB_RESULT(SUCCESS, region->Register(db));

        const UserStatisticHandle reads = ExpectHandle(FindUserStatistic(db, TEST_PROCESS_ID, "fs", "reads"));
        CHECK_EQUAL(TEST_PROCESS_ID, reads.ProcessId());
        CHECK_EQUAL(2u, reads.Id());                            //  the descriptor's id

        CHECK_EQUAL((int64_t)100, ExpectValue(reads));

        UserCounterIncrement(&Slot(section, 1), 1);             //  each call reads afresh
        CHECK_EQUAL((int64_t)101, ExpectValue(reads));

        CHECK_TRUE(reads.Exists());

        const char *name = nullptr;
        CHECK_TRUE(reads.Visit([&name](const UserStatistic &stat) { name = stat.Name(); }));
        STRCMP_EQUAL("reads", name);

        //  The process exits: the handle outlives its statistic, and says so.

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
        ReclaimRetiredUserStatisticsRegions();

        CHECK_DB_RESULT(NOT_FOUND, reads.Get().ResultCode());
        CHECK_FALSE(reads.Exists());
        CHECK_FALSE(reads.Visit([](const UserStatistic &) { FAIL("visited a removed statistic"); }));
    }

    TEST(UserStatisticHandleTests, HandleMatchesOnlyItsOwnKey)
    {
        UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), TEST_PROCESS_ID);
        CHECK(region != nullptr);
        CHECK_DB_RESULT(SUCCESS, region->Register(db));

        const UserStatisticHandle gauge = ExpectHandle(FindUserStatistic(db, TEST_PROCESS_ID, "fs", "open files"));

        CHECK_EQUAL((int64_t)7, ExpectValue(UserStatisticHandle(db, TEST_PROCESS_ID, gauge.Id())));

        //  The same id in another process, and another id in this one.

        CHECK_DB_RESULT(NOT_FOUND, UserStatisticHandle(db, TEST_PROCESS_ID + 1, gauge.Id()).Get().ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, UserStatisticHandle(db, TEST_PROCESS_ID, gauge.Id() + 100).Get().ResultCode());

        //  The right key in another database.

        SystemStatisticsDB other_db;
        CHECK_DB_RESULT(SUCCESS, other_db.Initialize(stats_db_test_resource));
        CHECK_DB_RESULT(NOT_FOUND, UserStatisticHandle(other_db, TEST_PROCESS_ID, gauge.Id()).Get().ResultCode());

        //  Kernel statistics are never reached through a handle.

        TestScalarStatistic compile_time(SystemStatistic::KERNEL_PROCESS_ID, 77, StatKind::GAUGE, "g", "n", "d");
        CHECK_DB_RESULT(SUCCESS, db.Register(compile_time));
        CHECK_FALSE(UserStatisticHandle(db, SystemStatistic::KERNEL_PROCESS_ID, 77).Exists());

        CHECK_DB_RESULT(NOT_FOUND, UserStatisticHandle().Get().ResultCode());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
    }

    TEST(UserStatisticHandleTests, DynamicStatisticsThatAreNotUserStatisticsAreNeverReached)
    {
        TestDynamicStatistic other(5);
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(other));

        //  The right key, but not a UserStatistic: the handle must not treat it as one.

        CHECK_FALSE(UserStatisticHandle(db, other.ProcessId(), other.id()).Exists());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, TEST_PROCESS_ID, "test", "dynamic").ResultCode());

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(other));
    }

    TEST(UserStatisticHandleTests, FindUserStatisticSelectsByProcessGroupAndName)
    {
        void *first_section = SectionWithThreeStatistics();
        void *second_section = SectionWithThreeStatistics();

        UserGaugeSet(&Slot(second_section, 0), 99);

        UserStatisticsRegion *first = CreateRegion(first_section, 7);
        UserStatisticsRegion *second = CreateRegion(second_section, 8);
        CHECK_DB_RESULT(SUCCESS, first->Register(db));
        CHECK_DB_RESULT(SUCCESS, second->Register(db));

        const UserStatisticHandle in_first = ExpectHandle(FindUserStatistic(db, 7, "fs", "open files"));
        const UserStatisticHandle in_second = ExpectHandle(FindUserStatistic(db, 8, "fs", "open files"));

        CHECK_EQUAL(in_first.Id(), in_second.Id());             //  one binary: the same ids, in two processes
        CHECK_EQUAL(7u, in_first.ProcessId());
        CHECK_EQUAL(8u, in_second.ProcessId());

        CHECK_EQUAL((int64_t)7, ExpectValue(in_first));
        CHECK_EQUAL((int64_t)99, ExpectValue(in_second));

        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, 9, "fs", "open files").ResultCode());       //  no such process
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, 7, "net", "open files").ResultCode());      //  no such group
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, 7, "fs", "writes").ResultCode());           //  no such name
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, 7, nullptr, "open files").ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, 7, "fs", nullptr).ResultCode());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(first));

        CHECK_FALSE(in_first.Exists());
        CHECK_TRUE(in_second.Exists());                         //  the other process is still running
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatistic(db, 7, "fs", "open files").ResultCode());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(second));
    }

    // =========================================================================
    //  Capacity: a database holds at most a fixed number of statistics
    // =========================================================================

    //  count TestDynamicStatistics from the test resource; the caller frees them with FreeDynamicStatistics().

    TestDynamicStatistic *AllocateDynamicStatistics(uint32_t count)
    {
        TestDynamicStatistic *statistics = static_cast<TestDynamicStatistic *>(stats_db_test_resource.allocate(count * sizeof(TestDynamicStatistic), alignof(TestDynamicStatistic)));

        for (uint32_t i = 0; i < count; i++)
        {
            new (&statistics[i]) TestDynamicStatistic(i);
        }

        return statistics;
    }

    void FreeDynamicStatistics(TestDynamicStatistic *statistics, uint32_t count)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            statistics[i].~TestDynamicStatistic();
        }

        stats_db_test_resource.deallocate(statistics, count * sizeof(TestDynamicStatistic), alignof(TestDynamicStatistic));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(StatsDBCapacityTests, StatsDBTestFixture)
    {
        void teardown() override
        {
            stats_db_test_resource.FailAllocations(false);
            ReclaimRetiredUserStatisticsRegions();
        }
    };
#pragma GCC diagnostic pop

    TEST(StatsDBCapacityTests, CapacityIsFixedByInitialize)
    {
        SystemStatisticsDB db;

        CHECK_EQUAL(0u, db.Capacity());
        CHECK_EQUAL(0u, db.Available());

        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, 3));
        CHECK_EQUAL(3u, db.Capacity());
        CHECK_EQUAL(3u, db.Available());

        SystemStatisticsDB default_db;
        CHECK_DB_RESULT(SUCCESS, default_db.Initialize(stats_db_test_resource));
        CHECK_EQUAL(SystemStatisticsDB::DEFAULT_CAPACITY, default_db.Capacity());
    }

    TEST(StatsDBCapacityTests, RegistrationPastCapacityIsRejected)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, 3));

        TestScalarStatistic compile_time(SystemStatistic::KERNEL_PROCESS_ID, 10, StatKind::GAUGE, "g", "a", "d");
        TestScalarStatistic compile_time_too_many(SystemStatistic::KERNEL_PROCESS_ID, 11, StatKind::GAUGE, "g", "b", "d");
        TestDynamicStatistic first(1);
        TestDynamicStatistic second(2);
        TestDynamicStatistic too_many(3);

        //  Compile-time and dynamic statistics share the capacity.

        CHECK_DB_RESULT(SUCCESS, db.Register(compile_time));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(first));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(second));
        CHECK_EQUAL(0u, db.Available());

        CHECK_DB_RESULT(DATABASE_FULL, db.RegisterDynamic(too_many));
        CHECK_DB_RESULT(DATABASE_FULL, db.Register(compile_time_too_many));
        CHECK_EQUAL(3u, db.Size());

        //  A duplicate is still a duplicate when the database is full.

        TestScalarStatistic same_id(SystemStatistic::KERNEL_PROCESS_ID, 10, StatKind::GAUGE, "g", "c", "d");
        CHECK_DB_RESULT(DUPLICATE_ID, db.Register(same_id));
        CHECK_DB_RESULT(DUPLICATE_ID, db.Register(compile_time));
        CHECK_DB_RESULT(DUPLICATE_ID, db.RegisterDynamic(first));

        //  Removing a statistic returns its room.

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(second));
        CHECK_EQUAL(1u, db.Available());
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(too_many));
        CHECK_EQUAL(0u, db.Available());

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(first));
        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(too_many));
    }

    TEST(StatsDBCapacityTests, FailedRegistrationsKeepNoRoom)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, 4));

        TestScalarStatistic compile_time(SystemStatistic::KERNEL_PROCESS_ID, 10, StatKind::GAUGE, "g", "a", "d");
        TestScalarStatistic same_id(SystemStatistic::KERNEL_PROCESS_ID, 10, StatKind::GAUGE, "g", "b", "d");

        CHECK_DB_RESULT(SUCCESS, db.Register(compile_time));
        CHECK_DB_RESULT(DUPLICATE_ID, db.Register(same_id));
        CHECK_EQUAL(3u, db.Available());

        //  The map cannot allocate a node: the insert fails, and its room is returned.

        TestDynamicStatistic dynamic(1);
        TestScalarStatistic compile_time_no_memory(SystemStatistic::KERNEL_PROCESS_ID, 12, StatKind::GAUGE, "g", "d", "d");

        stats_db_test_resource.FailAllocations(true);
        const SystemStatisticsDB::StatsDBResultCodes dynamic_result = db.RegisterDynamic(dynamic);
        const SystemStatisticsDB::StatsDBResultCodes compile_time_result = db.Register(compile_time_no_memory);
        stats_db_test_resource.FailAllocations(false);

        CHECK_DB_RESULT(FAILURE, dynamic_result);
        CHECK_DB_RESULT(FAILURE, compile_time_result);
        CHECK_EQUAL(3u, db.Available());
        CHECK_EQUAL(1u, db.Size());
    }

    TEST(StatsDBCapacityTests, ReservationIsAllOrNone)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, 4));

        TestDynamicStatistic statistics[3] = {TestDynamicStatistic(1), TestDynamicStatistic(2), TestDynamicStatistic(3)};

        {
            SystemStatisticsDB::Reservation too_large(db, 5);

            CHECK_FALSE(too_large.Granted());
            CHECK_EQUAL(4u, db.Available());                    //  nothing taken
            CHECK_DB_RESULT(DATABASE_FULL, too_large.RegisterDynamic(statistics[0]));
        }

        {
            SystemStatisticsDB::Reservation reservation(db, 3);

            CHECK_TRUE(reservation.Granted());
            CHECK_EQUAL(1u, db.Available());                    //  held for the reservation

            CHECK_DB_RESULT(SUCCESS, reservation.RegisterDynamic(statistics[0]));
            CHECK_DB_RESULT(SUCCESS, reservation.RegisterDynamic(statistics[1]));
            CHECK_DB_RESULT(DUPLICATE_ID, reservation.RegisterDynamic(statistics[1]));   //  uses none of the room
            CHECK_EQUAL(1u, db.Available());
        }

        //  The unused third place is returned when the reservation ends.

        CHECK_EQUAL(2u, db.Available());
        CHECK_EQUAL(2u, db.Size());

        {
            SystemStatisticsDB::Reservation empty(db, 0);
            CHECK_TRUE(empty.Granted());
            CHECK_DB_RESULT(DATABASE_FULL, empty.RegisterDynamic(statistics[2]));
        }

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(statistics[0]));
        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(statistics[1]));
        CHECK_EQUAL(4u, db.Available());
    }

    TEST(StatsDBCapacityTests, ProcessStatisticsAreRegisteredAllOrNone)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, 5));

        UserStatisticsRegion *first = CreateRegion(SectionWithThreeStatistics(), 1);
        UserStatisticsRegion *second = CreateRegion(SectionWithThreeStatistics(), 2);

        CHECK_DB_RESULT(SUCCESS, first->Register(db));
        CHECK_EQUAL(2u, db.Available());

        //  Room for two of the second process's three: it gets none.

        CHECK_DB_RESULT(DATABASE_FULL, second->Register(db));
        CHECK_EQUAL(0u, second->Registered());
        CHECK_EQUAL(2u, db.Available());
        CHECK_EQUAL(3u, db.Size());

        //  The first process exits; a third process now fits.

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(first));
        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(second));
        CHECK_EQUAL(5u, db.Available());

        UserStatisticsRegion *third = CreateRegion(SectionWithThreeStatistics(), 3);
        CHECK_DB_RESULT(SUCCESS, third->Register(db));
        CHECK_EQUAL(3u, third->Registered());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(third));
    }

    TEST(StatsDBCapacityTests, RegionInAnUninitializedDatabaseIsNotFull)
    {
        SystemStatisticsDB db;

        UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), 1);

        CHECK_DB_RESULT(NOT_INITIALIZED, region->Register(db));
        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(region));
    }

    TEST(StatsDBCapacityTests, FullDatabaseMemoryIsBounded)
    {
        //  Enough statistics for the per-statistic share of the slot blocks to show.

        constexpr uint32_t CAPACITY = 2048;

        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, CAPACITY));

        TestDynamicStatistic *statistics = AllocateDynamicStatistics(CAPACITY + 1);

        const size_t bytes_before = stats_db_test_resource.BytesInUse();

        for (uint32_t i = 0; i < CAPACITY; i++)
        {
            CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(statistics[i]));
        }

        CHECK_DB_RESULT(DATABASE_FULL, db.RegisterDynamic(statistics[CAPACITY]));

        const size_t bytes_used = stats_db_test_resource.BytesInUse() - bytes_before;

        CHECK(bytes_used <= static_cast<size_t>(CAPACITY) * SystemStatisticsDB::MEMORY_PER_STATISTIC);

        for (uint32_t i = 0; i < CAPACITY; i++)
        {
            CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(statistics[i]));
        }

        CHECK_EQUAL(CAPACITY, db.Available());

        FreeDynamicStatistics(statistics, CAPACITY + 1);
    }

    //  Writers race to fill a small database: exactly its capacity get in.

    constexpr uint32_t CAPACITY_RACE_WRITERS = 4;
    constexpr uint32_t CAPACITY_RACE_PER_WRITER = 64;
    constexpr uint32_t CAPACITY_RACE_CAPACITY = 100;

    struct capacity_race_arg
    {
        SystemStatisticsDB      *db_;
        TestDynamicStatistic    *statistics_;
        uint32_t                 registered_count_;
        uint32_t                 full_count_;
    };

    void *capacity_race_worker(void *raw_arg)
    {
        capacity_race_arg *arg = static_cast<capacity_race_arg *>(raw_arg);

        for (uint32_t i = 0; i < CAPACITY_RACE_PER_WRITER; i++)
        {
            const SystemStatisticsDB::StatsDBResultCodes result = arg->db_->RegisterDynamic(arg->statistics_[i]);

            arg->registered_count_ += (result == SystemStatisticsDB::StatsDBResultCodes::SUCCESS) ? 1 : 0;
            arg->full_count_ += (result == SystemStatisticsDB::StatsDBResultCodes::DATABASE_FULL) ? 1 : 0;
        }

        return nullptr;
    }

    TEST(StatsDBCapacityTests, ConcurrentRegistrationNeverExceedsCapacity)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource, CAPACITY_RACE_CAPACITY));

        TestDynamicStatistic *statistics = AllocateDynamicStatistics(CAPACITY_RACE_WRITERS * CAPACITY_RACE_PER_WRITER);

        pthread_t threads[CAPACITY_RACE_WRITERS];
        capacity_race_arg args[CAPACITY_RACE_WRITERS];

        for (uint32_t writer_index = 0; writer_index < CAPACITY_RACE_WRITERS; writer_index++)
        {
            args[writer_index] = capacity_race_arg{&db, &statistics[writer_index * CAPACITY_RACE_PER_WRITER], 0, 0};
            pthread_create(&threads[writer_index], nullptr, capacity_race_worker, &args[writer_index]);
        }

        uint32_t registered_count = 0;
        uint32_t full_count = 0;

        for (uint32_t writer_index = 0; writer_index < CAPACITY_RACE_WRITERS; writer_index++)
        {
            pthread_join(threads[writer_index], nullptr);
            registered_count += args[writer_index].registered_count_;
            full_count += args[writer_index].full_count_;
        }

        CHECK_EQUAL(CAPACITY_RACE_CAPACITY, registered_count);
        CHECK_EQUAL(CAPACITY_RACE_WRITERS * CAPACITY_RACE_PER_WRITER - CAPACITY_RACE_CAPACITY, full_count);
        CHECK_EQUAL(CAPACITY_RACE_CAPACITY, db.Size());
        CHECK_EQUAL(0u, db.Available());

        uint32_t removed_count = 0;

        for (uint32_t i = 0; i < CAPACITY_RACE_WRITERS * CAPACITY_RACE_PER_WRITER; i++)
        {
            removed_count += (db.RemoveDynamic(statistics[i]) == SystemStatisticsDB::StatsDBResultCodes::SUCCESS) ? 1 : 0;
        }

        CHECK_EQUAL(CAPACITY_RACE_CAPACITY, removed_count);
        CHECK_EQUAL(CAPACITY_RACE_CAPACITY, db.Available());

        FreeDynamicStatistics(statistics, CAPACITY_RACE_WRITERS * CAPACITY_RACE_PER_WRITER);
    }

    // =========================================================================
    //  Records: what the statistics system calls return, and to whom
    // =========================================================================

    constexpr uint32_t OWNER_PROCESS_ID = 7;
    constexpr uint32_t OTHER_PROCESS_ID = 8;

    //  Slots 0-2 private, as SectionWithThreeStatistics(); slot 3 public.

    void *SectionWithAPublicStatistic()
    {
        void *section = SectionWithThreeStatistics();

        Describe(section, 3, USER_STATISTIC_GAUGE, "fs", "cache size", "Blocks cached", 64);
        Slot(section, 3).flags_ = USER_STATISTIC_PUBLIC;

        return section;
    }

    //  The bytes from the terminator to the end of the array.

    bool TailIsZero(const char *string, size_t length)
    {
        for (size_t i = strnlen(string, length); i < length; i++)
        {
            if (string[i] != 0)
            {
                return false;
            }
        }

        return true;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(UserStatisticRecordTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;
        UserStatisticsRegion *owner = nullptr;
        UserStatisticsRegion *other = nullptr;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));

            owner = CreateRegion(SectionWithAPublicStatistic(), OWNER_PROCESS_ID);
            other = CreateRegion(SectionWithAPublicStatistic(), OTHER_PROCESS_ID);
            CHECK_DB_RESULT(SUCCESS, owner->Register(db));
            CHECK_DB_RESULT(SUCCESS, other->Register(db));
        }

        void teardown() override
        {
            RetireUserStatisticsRegion(owner);
            RetireUserStatisticsRegion(other);
            ReclaimRetiredUserStatisticsRegions();
        }
    };
#pragma GCC diagnostic pop

    TEST(UserStatisticRecordTests, OwnerReadsItsPrivateStatistics)
    {
        const UserStatisticReader reader = UserStatisticReader::Process(OWNER_PROCESS_ID);

        UserStatisticRecord record = ExpectRecord(FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "reads"));

        const UserStatisticHandle reads = ExpectHandle(FindUserStatistic(db, OWNER_PROCESS_ID, "fs", "reads"));

        CHECK_EQUAL(reads.Id(), record.id_);
        CHECK_EQUAL(OWNER_PROCESS_ID, record.process_id_);
        CHECK_EQUAL((int64_t)100, record.value_);
        CHECK_EQUAL(USER_STATISTIC_COUNTER, record.kind_);
        CHECK_EQUAL(USER_STATISTIC_PRIVATE, record.flags_);
        STRCMP_EQUAL("fs", record.group_);
        STRCMP_EQUAL("reads", record.name_);
        STRCMP_EQUAL("Blocks read", record.description_);

        //  Read by process id and id returns the current value.

        reads.Visit([](const UserStatistic &stat) { stat.SetValue(250); });

        record = ExpectRecord(ReadUserStatisticRecord(db, reader, OWNER_PROCESS_ID, reads.Id()));
        CHECK_EQUAL((int64_t)250, record.value_);
        STRCMP_EQUAL("reads", record.name_);
    }

    TEST(UserStatisticRecordTests, OtherProcessesSeeOnlyPublicStatistics)
    {
        const UserStatisticReader reader = UserStatisticReader::Process(OTHER_PROCESS_ID);
        UserStatisticRecord record;

        const UserStatisticHandle reads = ExpectHandle(FindUserStatistic(db, OWNER_PROCESS_ID, "fs", "reads"));
        const UserStatisticHandle cache = ExpectHandle(FindUserStatistic(db, OWNER_PROCESS_ID, "fs", "cache size"));

        //  Private: reported exactly as absent, by every route.

        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "reads").ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, OWNER_PROCESS_ID, reads.Id()).ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "no such name").ResultCode());

        //  Public: readable by anyone.

        record = ExpectRecord(FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "cache size"));
        CHECK_EQUAL(cache.Id(), record.id_);
        CHECK_EQUAL(OWNER_PROCESS_ID, record.process_id_);
        CHECK_EQUAL(USER_STATISTIC_PUBLIC, record.flags_);
        CHECK_EQUAL((int64_t)64, record.value_);

        record = ExpectRecord(ReadUserStatisticRecord(db, reader, OWNER_PROCESS_ID, cache.Id()));
        CHECK_EQUAL(cache.Id(), record.id_);
        CHECK_EQUAL(OWNER_PROCESS_ID, record.process_id_);
    }

    TEST(UserStatisticRecordTests, KernelReaderSeesEveryStatistic)
    {
        const UserStatisticReader reader = UserStatisticReader::Kernel();
        UserStatisticRecord record;

        record = ExpectRecord(FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "reads"));
        record = ExpectRecord(FindUserStatisticRecord(db, reader, OTHER_PROCESS_ID, "fs", "open files"));
        CHECK_EQUAL(OTHER_PROCESS_ID, record.process_id_);
    }

    TEST(UserStatisticRecordTests, ProcessZeroOwnsNothing)
    {
        //  Process 0 is the kernel's: no user statistics are created for it, and a reader with id 0 owns nothing.

        void *section = SectionWithAPublicStatistic();

        CHECK_DB_RESULT(INVALID_PROCESS_ID, UserStatisticsRegion::Create(section, SECTION_SIZE, SystemStatistic::KERNEL_PROCESS_ID, ReleaseSection, stats_db_test_resource).ResultCode());
        stats_db_test_resource.deallocate(section, SECTION_SIZE, 64);

        const UserStatisticReader reader = UserStatisticReader::Process(0);
        UserStatisticRecord record;

        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "reads").ResultCode());
        record = ExpectRecord(FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "cache size"));
    }

    TEST(UserStatisticRecordTests, AbsentOrRemovedStatisticsAreNotFound)
    {
        const UserStatisticReader reader = UserStatisticReader::Kernel();
        const UserStatisticHandle gauge = ExpectHandle(FindUserStatistic(db, OWNER_PROCESS_ID, "fs", "open files"));

        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, 99, gauge.Id()).ResultCode());        //  no such process
        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, OWNER_PROCESS_ID, 0).ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, OWNER_PROCESS_ID, 0xFFFFFFFF).ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, 0, 0).ResultCode());

        CHECK_DB_RESULT(SUCCESS, RetireUserStatisticsRegion(owner));
        owner = nullptr;

        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, OWNER_PROCESS_ID, gauge.Id()).ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", "open files").ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, nullptr, "open files").ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, OWNER_PROCESS_ID, "fs", nullptr).ResultCode());
    }

    TEST(UserStatisticRecordTests, KernelAndOtherDynamicStatisticsAreNeverReturned)
    {
        TestScalarStatistic compile_time(SystemStatistic::KERNEL_PROCESS_ID, 77, StatKind::GAUGE, "fs", "reads", "A kernel statistic");
        TestDynamicStatistic not_a_user_statistic(5);

        CHECK_DB_RESULT(SUCCESS, db.Register(compile_time));
        CHECK_DB_RESULT(SUCCESS, db.RegisterDynamic(not_a_user_statistic));

        const UserStatisticReader reader = UserStatisticReader::Kernel();
        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, SystemStatistic::KERNEL_PROCESS_ID, 77).ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, ReadUserStatisticRecord(db, reader, not_a_user_statistic.ProcessId(), not_a_user_statistic.id()).ResultCode());
        CHECK_DB_RESULT(NOT_FOUND, FindUserStatisticRecord(db, reader, not_a_user_statistic.ProcessId(), "test", "dynamic").ResultCode());

        CHECK_DB_RESULT(SUCCESS, db.RemoveDynamic(not_a_user_statistic));
    }

    TEST(UserStatisticRecordTests, RecordHoldsNothingButTheStatistic)
    {
        //  Every byte the statistic does not fill -- after each terminator, the reserved tail -- is zero.

        const UserStatisticRecord record = ExpectRecord(FindUserStatisticRecord(db, UserStatisticReader::Kernel(), OWNER_PROCESS_ID, "fs", "reads"));

        CHECK_TRUE(TailIsZero(record.group_, sizeof(record.group_)));
        CHECK_TRUE(TailIsZero(record.name_, sizeof(record.name_)));
        CHECK_TRUE(TailIsZero(record.description_, sizeof(record.description_)));

        for (size_t i = 0; i < sizeof(record.reserved_); i++)
        {
            CHECK_EQUAL(0, record.reserved_[i]);
        }
    }

    // =========================================================================
    //  Concurrency: regions come and go under constant reading
    // =========================================================================

    constexpr uint32_t CHURN_READERS = 3;
    constexpr uint32_t CHURN_HANDLE_READERS = 2;
    constexpr uint32_t CHURN_READS_PER_HANDLE = 8;
    constexpr uint32_t CHURN_ROUNDS = 2000;

    struct churn_shared
    {
        SystemStatisticsDB      *db_;
        minstd::atomic<bool>     done_{false};
    };

    struct churn_reader_arg
    {
        churn_shared *shared_;
        uint64_t      reads_;
        uint64_t      poisoned_;
    };

    constexpr uint32_t CHURN_DWELL_SPINS = 2000;

    //  Reads every value continuously, and again after lingering on the statistic, as a slow reader would.
    //      Retired regions are poisoned as they are freed, so a reader that can still reach a freed region reads
    //      POISON.

    static void *churn_reader_worker(void *arg)
    {
        auto *reader_arg = static_cast<churn_reader_arg *>(arg);

        while (!reader_arg->shared_->done_.load(minstd::memory_order_acquire))
        {
            reader_arg->shared_->db_->ForEach([reader_arg](const SystemStatistic &stat)
            {
                const int64_t value = ValueOf(stat);

                for (volatile uint32_t spin = 0; spin < CHURN_DWELL_SPINS; spin++)
                {
                }

                const int64_t value_after_dwell = ValueOf(stat);

                reader_arg->reads_++;

                if ((value == POISON) || (value_after_dwell == POISON))
                {
                    reader_arg->poisoned_++;
                }

                return SystemStatisticsDB::VisitAction::CONTINUE;
            });
        }

        return nullptr;
    }

    struct churn_handle_reader_arg
    {
        churn_shared *shared_;
        uint64_t      reads_;
        uint64_t      gone_;
        uint64_t      poisoned_;
    };

    //  Keeps a handle across many calls, as a monitor would, while its statistic is retired and freed under it:
    //      every call must either read a live value or report the statistic gone.

    static void *churn_handle_reader_worker(void *arg)
    {
        auto *reader_arg = static_cast<churn_handle_reader_arg *>(arg);

        while (!reader_arg->shared_->done_.load(minstd::memory_order_acquire))
        {
            const auto found = FindUserStatistic(*reader_arg->shared_->db_, TEST_PROCESS_ID, "fs", "open files");

            if (found.Failed())
            {
                continue;
            }

            const UserStatisticHandle handle = *found;

            for (uint32_t i = 0; i < CHURN_READS_PER_HANDLE; i++)
            {
                const auto value = handle.Get();

                if (value.Failed())
                {
                    reader_arg->gone_++;
                    break;
                }

                reader_arg->reads_++;

                if (*value == POISON)
                {
                    reader_arg->poisoned_++;
                }

                for (volatile uint32_t spin = 0; spin < CHURN_DWELL_SPINS; spin++)
                {
                }
            }
        }

        return nullptr;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(UserStatisticsConcurrencyTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(UserStatisticsConcurrencyTests, RetiredRegionsAreNeverReadAfterTheyAreFreed)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));

        const uint32_t live_before = UserStatisticsRegion::LiveRegions();

        churn_shared shared;
        shared.db_ = &db;

        pthread_t readers[CHURN_READERS];
        churn_reader_arg reader_args[CHURN_READERS];
        pthread_t handle_readers[CHURN_HANDLE_READERS];
        churn_handle_reader_arg handle_reader_args[CHURN_HANDLE_READERS];

        for (uint32_t i = 0; i < CHURN_READERS; i++)
        {
            reader_args[i] = {&shared, 0, 0};
            pthread_create(&readers[i], nullptr, churn_reader_worker, &reader_args[i]);
        }

        for (uint32_t i = 0; i < CHURN_HANDLE_READERS; i++)
        {
            handle_reader_args[i] = {&shared, 0, 0, 0};
            pthread_create(&handle_readers[i], nullptr, churn_handle_reader_worker, &handle_reader_args[i]);
        }

        uint32_t failure_count = 0;

        for (uint32_t round = 0; round < CHURN_ROUNDS; round++)
        {
            UserStatisticsRegion *region = CreateRegion(SectionWithThreeStatistics(), TEST_PROCESS_ID);

            if ((region == nullptr) ||
                (region->Register(db) != SystemStatisticsDB::StatsDBResultCodes::SUCCESS) ||
                (RetireUserStatisticsRegion(region) != SystemStatisticsDB::StatsDBResultCodes::SUCCESS))
            {
                failure_count++;
            }

            ReclaimRetiredUserStatisticsRegions();
        }

        shared.done_.store(true, minstd::memory_order_release);

        uint64_t total_reads = 0;

        for (uint32_t i = 0; i < CHURN_READERS; i++)
        {
            pthread_join(readers[i], nullptr);
            CHECK_EQUAL(0u, reader_args[i].poisoned_);
            total_reads += reader_args[i].reads_;
        }

        uint64_t handle_reads = 0;
        uint64_t handles_gone = 0;

        for (uint32_t i = 0; i < CHURN_HANDLE_READERS; i++)
        {
            pthread_join(handle_readers[i], nullptr);
            CHECK_EQUAL(0u, handle_reader_args[i].poisoned_);
            handle_reads += handle_reader_args[i].reads_;
            handles_gone += handle_reader_args[i].gone_;
        }

        CHECK_EQUAL(0u, failure_count);
        CHECK(total_reads > 0);
        CHECK(handle_reads > 0);
        CHECK(handles_gone > 0);                                //  handles really did outlive their statistics

        ReclaimRetiredUserStatisticsRegions();                  //  readers are gone: everything can go

        CHECK_EQUAL(0u, db.Size());
        CHECK_EQUAL(live_before, UserStatisticsRegion::LiveRegions());
    }

    // =========================================================================
    //  UserStatistic as a scalar, dynamic statistic
    // =========================================================================

    template<typename Statistic>
    concept CanRegisterDynamicStatistic = requires(SystemStatisticsDB &db, Statistic &stat) { db.RegisterDynamic(stat); };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(UserStatisticViewTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(UserStatisticViewTests, UserStatisticsAreDynamicScalarStatistics)
    {
        static_assert(minstd::derived_from<UserStatistic, ScalarStatisticView>);
        static_assert(DynamicStatisticType<UserStatistic>);
        static_assert(DynamicStatisticType<TestDynamicStatistic>);
        static_assert(CanRegisterDynamicStatistic<UserStatistic>);
        static_assert(CanRegisterDynamicStatistic<TestDynamicStatistic>);
        static_assert(!DynamicStatisticType<DynamicStatistic>);             //  the marker alone is not a statistic
        static_assert(!DynamicStatisticType<TestScalarStatistic>);          //  a statistic without the marker
    }

    TEST(UserStatisticViewTests, DynamicStatisticsAreReadThroughTheirView)
    {
        TestDynamicStatistic stat(9);
        const SystemStatistic &base = stat;

        POINTERS_EQUAL(&stat, base.As<ScalarStatisticView>());
        POINTERS_EQUAL(nullptr, base.As<ArrayStatisticView>());
        POINTERS_EQUAL(nullptr, base.AsUserStatistic());

        const auto value = base.GetValue<int64_t>();
        CHECK_TRUE(value.Successful());
        CHECK_EQUAL((int64_t)0, *value);
    }
}
