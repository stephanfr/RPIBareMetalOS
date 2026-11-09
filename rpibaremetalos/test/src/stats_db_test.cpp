// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"
#include "stats_db_test_support.h"

#include "services/stats_db/system_stats.h"
#include "services/stats_db/stats_db.h"
#include "services/stats_db/kernel_statistic_ids.h"

namespace
{
    using namespace system_stats;
    using minstd::lockfree::latest_value_result;

#define CHECK_RESULT(expected, expression) CHECK_EQUAL(static_cast<int>(latest_value_result::expected), static_cast<int>(expression))
#define CHECK_DB_RESULT(expected, expression) CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::expected), static_cast<int>(expression))

    //  Ids for the global test statistics.  Ids must be unique across every test file: all statically constructed
    //      statistics land in one recorded list and one global database.

    constexpr uint32_t GAUGE_A_ID     = 1001;
    constexpr uint32_t COUNTER_A_ID   = 1002;
    constexpr uint32_t GAUGE_B_ID     = 1003;
    constexpr uint32_t COUNTER_B_ID   = 1005;
    constexpr uint32_t TIMESTAMP_A_ID = 1006;

    //  Global static statistics (internal linkage via the anonymous namespace), mirroring the production pattern of
    //      global metric objects that live for the life of the kernel.  They record themselves during static
    //      initialisation and StatsDBTestFixture registers them in GetSystemStatisticsDB(), as InitializePlatform() does.

    GaugeStatistic<GAUGE_A_ID>          gauge_a_stat( "memory", "free pages", "Free pages in the allocator", 0 );
    CounterStatistic<COUNTER_A_ID>      counter_a_stat( "tasks", "zombies dropped", "Dead tasks never reclaimed", 0 );
    GaugeStatistic<GAUGE_B_ID>          gauge_b_stat( "memory", "asids held", "ASIDs held by live address spaces", 100 );
    CounterStatistic<COUNTER_B_ID>      counter_b_stat( "tasks", "user faults", "User tasks killed by a fault", 7 );
    TimestampStatistic<TIMESTAMP_A_ID>  timestamp_a_stat( "memory", "last invalid release", "Time of most recent invalid release" );

    bool IsRecorded(const SystemStatistic &stat)
    {
        for (const StaticStatisticLink *link = StaticStatisticLink::FirstStaticStatistic(); link != nullptr; link = link->NextStaticStatistic())
        {
            if (&link->Statistic() == &stat)
            {
                return true;
            }
        }

        return false;
    }

    uint32_t RecordedCount()
    {
        uint32_t count = 0;

        for (const StaticStatisticLink *link = StaticStatisticLink::FirstStaticStatistic(); link != nullptr; link = link->NextStaticStatistic())
        {
            count++;
        }

        return count;
    }

    //  A database populated with the five global statistics, registered out of id order.

    void RegisterGlobals(SystemStatisticsDB &db)
    {
        CHECK_DB_RESULT(SUCCESS, db.Register(timestamp_a_stat));
        CHECK_DB_RESULT(SUCCESS, db.Register(gauge_b_stat));
        CHECK_DB_RESULT(SUCCESS, db.Register(gauge_a_stat));
        CHECK_DB_RESULT(SUCCESS, db.Register(counter_b_stat));
        CHECK_DB_RESULT(SUCCESS, db.Register(counter_a_stat));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(GaugeStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(GaugeStatisticTests, InitialValueReadBackBeforeAnyWrite)
    {
        GaugeStatistic<10> gauge("memory", "pages", "desc", 42);

        CHECK_EQUAL((int64_t)42, gauge.Get());
    }

    TEST(GaugeStatisticTests, SetReplacesValue)
    {
        GaugeStatistic<10> gauge("memory", "pages", "desc", 0);

        gauge.Set(1);
        CHECK_EQUAL((int64_t)1, gauge.Get());

        gauge.Set(-12345);
        CHECK_EQUAL((int64_t)-12345, gauge.Get());

        gauge.Set(0);
        CHECK_EQUAL((int64_t)0, gauge.Get());
    }

    TEST(GaugeStatisticTests, KindAndRepresentationReportGauge)
    {
        CHECK_EQUAL(static_cast<int>(StatKind::GAUGE), static_cast<int>(gauge_a_stat.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(gauge_a_stat.Representation()));
    }

    TEST(GaugeStatisticTests, MetadataExposedThroughBasePointer)
    {
        const SystemStatistic &base = gauge_a_stat;

        CHECK_EQUAL(GAUGE_A_ID, base.id());
        STRCMP_EQUAL("memory", base.Group());
        STRCMP_EQUAL("free pages", base.Name());
        STRCMP_EQUAL("Free pages in the allocator", base.Description());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(CounterStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(CounterStatisticTests, InitialValueReadBackBeforeAnyIncrement)
    {
        CounterStatistic<20> counter("tasks", "faults", "desc", 7);

        CHECK_EQUAL((int64_t)7, counter.Get());
    }

    TEST(CounterStatisticTests, IncrementDefaultAddsOne)
    {
        CounterStatistic<20> counter("tasks", "faults", "desc", 0);

        counter.Increment();
        CHECK_EQUAL((int64_t)1, counter.Get());

        counter.Increment();
        counter.Increment();
        CHECK_EQUAL((int64_t)3, counter.Get());
    }

    TEST(CounterStatisticTests, IncrementByAmountAccumulates)
    {
        CounterStatistic<20> counter("tasks", "faults", "desc", 10);

        counter.Increment(5);
        CHECK_EQUAL((int64_t)15, counter.Get());

        counter.Increment(10);
        CHECK_EQUAL((int64_t)25, counter.Get());
    }

    TEST(CounterStatisticTests, IncrementReturnsPreviousValue)
    {
        CounterStatistic<20> counter("tasks", "faults", "desc", 100);

        const int64_t returned = counter.Increment(5);

        CHECK_EQUAL((int64_t)100, returned);   // fetch_add: old value
        CHECK_EQUAL((int64_t)105, counter.Get());
    }

    TEST(CounterStatisticTests, KindAndRepresentationReportCounter)
    {
        CHECK_EQUAL(static_cast<int>(StatKind::COUNTER), static_cast<int>(counter_a_stat.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(counter_a_stat.Representation()));
    }

    TEST(CounterStatisticTests, MetadataExposedThroughBasePointer)
    {
        const SystemStatistic &base = counter_b_stat;

        CHECK_EQUAL(COUNTER_B_ID, base.id());
        STRCMP_EQUAL("tasks", base.Group());
        STRCMP_EQUAL("user faults", base.Name());
        STRCMP_EQUAL("User tasks killed by a fault", base.Description());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(StaticStatisticRecordingTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(StaticStatisticRecordingTests, StaticallyConstructedStatisticsAreRecorded)
    {
        CHECK_TRUE(IsRecorded(gauge_a_stat));
        CHECK_TRUE(IsRecorded(counter_a_stat));
        CHECK_TRUE(IsRecorded(gauge_b_stat));
        CHECK_TRUE(IsRecorded(counter_b_stat));
        CHECK_TRUE(IsRecorded(timestamp_a_stat));
    }

    TEST(StaticStatisticRecordingTests, StatisticsConstructedAfterStartupAreNotRecorded)
    {
        CHECK_FALSE(StaticStatisticLink::StaticRecordingOpen());     //  closed by the fixture's RegisterStaticStatistics()

        const uint32_t recorded_before = RecordedCount();
        GaugeStatistic<1100> local_gauge("test", "local", "Constructed on the stack", 0);

        CHECK_FALSE(IsRecorded(local_gauge));
        CHECK_EQUAL(recorded_before, RecordedCount());
    }

    TEST(StaticStatisticRecordingTests, GlobalDatabaseHoldsEveryRecordedStatistic)
    {
        SystemStatisticsDB &db = GetSystemStatisticsDB();

        POINTERS_EQUAL(&gauge_a_stat, FoundStatistic(db, GAUGE_A_ID));
        POINTERS_EQUAL(&counter_a_stat, FoundStatistic(db, COUNTER_A_ID));
        POINTERS_EQUAL(&gauge_b_stat, FoundStatistic(db, GAUGE_B_ID));
        POINTERS_EQUAL(&counter_b_stat, FoundStatistic(db, COUNTER_B_ID));
        POINTERS_EQUAL(&timestamp_a_stat, FoundStatistic(db, TIMESTAMP_A_ID));

        for (const StaticStatisticLink *link = StaticStatisticLink::FirstStaticStatistic(); link != nullptr; link = link->NextStaticStatistic())
        {
            POINTERS_EQUAL(&link->Statistic(), FoundStatistic(db, link->Statistic().id()));
        }
    }

    TEST(StaticStatisticRecordingTests, RegisterStaticStatisticsFillsAnotherDatabase)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));

        StaticRegistrationSummary summary;
        CHECK_DB_RESULT(SUCCESS, RegisterStaticStatistics(db, summary));

        CHECK_EQUAL(RecordedCount(), summary.registered_count_);
        CHECK_EQUAL(0u, summary.already_registered_count_);
        CHECK_EQUAL(0u, summary.duplicate_count_);                    //  test-wide ids are unique
        CHECK_EQUAL(0u, summary.failed_count_);
        CHECK_EQUAL(RecordedCount(), db.Size());
        POINTERS_EQUAL(&gauge_a_stat, FoundStatistic(db, GAUGE_A_ID));
    }

    TEST(StaticStatisticRecordingTests, RegisteringStatisticsAlreadyInTheDatabaseSucceeds)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        CHECK_DB_RESULT(SUCCESS, db.Register(gauge_a_stat));

        StaticRegistrationSummary summary;
        CHECK_DB_RESULT(SUCCESS, RegisterStaticStatistics(db, summary));

        CHECK_EQUAL(RecordedCount() - 1, summary.registered_count_);
        CHECK_EQUAL(1u, summary.already_registered_count_);
        CHECK_EQUAL(0u, summary.duplicate_count_);

        CHECK_DB_RESULT(SUCCESS, RegisterStaticStatistics(db, summary));     //  a second pass finds every one present

        CHECK_EQUAL(0u, summary.registered_count_);
        CHECK_EQUAL(RecordedCount(), summary.already_registered_count_);
        CHECK_EQUAL(RecordedCount(), db.Size());
    }

    TEST(StaticStatisticRecordingTests, RegisterStaticStatisticsReportsDuplicatesAndFailures)
    {
        SystemStatisticsDB db;
        CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));

        TestScalarStatistic impostor(SystemStatistic::KERNEL_PROCESS_ID, GAUGE_A_ID, StatKind::GAUGE, "impostor", "same id", "Registered first");
        CHECK_DB_RESULT(SUCCESS, db.Register(impostor));

        StaticRegistrationSummary summary;
        CHECK_DB_RESULT(DUPLICATE_ID, RegisterStaticStatistics(db, summary));

        CHECK_EQUAL(RecordedCount() - 1, summary.registered_count_);    //  the rest are still registered
        CHECK_EQUAL(1u, summary.duplicate_count_);
        CHECK_EQUAL(GAUGE_A_ID, summary.first_duplicate_id_);
        CHECK_EQUAL(0u, summary.failed_count_);
        POINTERS_EQUAL(&impostor, FoundStatistic(db, GAUGE_A_ID));          //  the first registration keeps the id

        SystemStatisticsDB uninitialized_db;
        StaticRegistrationSummary not_initialized;
        CHECK_DB_RESULT(NOT_INITIALIZED, RegisterStaticStatistics(uninitialized_db, not_initialized));

        CHECK_EQUAL(0u, not_initialized.registered_count_);
        CHECK_EQUAL(RecordedCount(), not_initialized.failed_count_);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(SystemStatisticsDBTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        }
    };
#pragma GCC diagnostic pop

    TEST(SystemStatisticsDBTests, FreshDatabaseIsEmpty)
    {
        CHECK_TRUE(db.Empty());
        CHECK_EQUAL(0u, db.Size());
        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 0));
    }

    TEST(SystemStatisticsDBTests, OperationsBeforeInitializeFailCleanly)
    {
        SystemStatisticsDB uninitialized_db;

        CHECK_DB_RESULT(NOT_INITIALIZED, uninitialized_db.Register(gauge_a_stat));
        CHECK_TRUE(uninitialized_db.Empty());
        CHECK_EQUAL(0u, uninitialized_db.Size());
        CHECK_FALSE(uninitialized_db.Contains(SystemStatistic::KERNEL_PROCESS_ID, GAUGE_A_ID));
        CHECK_FALSE(uninitialized_db.Visit(SystemStatistic::KERNEL_PROCESS_ID, GAUGE_A_ID, [](const SystemStatistic &) {}));

        uint32_t visited = 0;
        uninitialized_db.ForEach([&visited](const SystemStatistic &) { visited++; return SystemStatisticsDB::VisitAction::CONTINUE; });
        CHECK_EQUAL(0u, visited);

        CHECK_DB_RESULT(NOT_INITIALIZED, uninitialized_db.Schema().ResultCode());
    }

    TEST(SystemStatisticsDBTests, InitializeWithoutMemoryFailsAndCanBeRetried)
    {
        SystemStatisticsDB retry_db;

        stats_db_test_resource.FailAllocations(true);
        const SystemStatisticsDB::StatsDBResultCodes no_memory = retry_db.Initialize(stats_db_test_resource);
        stats_db_test_resource.FailAllocations(false);

        CHECK_DB_RESULT(UNABLE_TO_ALLOCATE_DATABASE, no_memory);
        CHECK_DB_RESULT(NOT_INITIALIZED, retry_db.Register(gauge_a_stat));
        CHECK_EQUAL(0u, retry_db.Size());

        CHECK_DB_RESULT(SUCCESS, retry_db.Initialize(stats_db_test_resource));
        CHECK_DB_RESULT(SUCCESS, retry_db.Register(gauge_a_stat));
        POINTERS_EQUAL(&gauge_a_stat, FoundStatistic(retry_db, GAUGE_A_ID));
    }

    TEST(SystemStatisticsDBTests, SecondInitializeRejected)
    {
        CHECK_DB_RESULT(ALREADY_INITIALIZED, db.Initialize(stats_db_test_resource));

        CHECK_DB_RESULT(SUCCESS, db.Register(gauge_a_stat));      //  the first map is still in use
        POINTERS_EQUAL(&gauge_a_stat, FoundStatistic(db, GAUGE_A_ID));
    }

    TEST(SystemStatisticsDBTests, RegisteredStatisticsAreAllFound)
    {
        RegisterGlobals(db);

        CHECK_FALSE(db.Empty());
        CHECK_EQUAL(5u, db.Size());
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, GAUGE_A_ID));
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, COUNTER_A_ID));
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, GAUGE_B_ID));
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, COUNTER_B_ID));
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, TIMESTAMP_A_ID));
    }

    TEST(SystemStatisticsDBTests, VisitReachesExactRegisteredObject)
    {
        RegisterGlobals(db);

        const SystemStatistic *found = FoundStatistic(db, GAUGE_A_ID);

        POINTERS_EQUAL(&gauge_a_stat, found);                   //  the very global we registered
        CHECK_EQUAL(GAUGE_A_ID, found->id());
        STRCMP_EQUAL("memory", found->Group());
        STRCMP_EQUAL("free pages", found->Name());
        STRCMP_EQUAL("Free pages in the allocator", found->Description());
    }

    TEST(SystemStatisticsDBTests, VisitSkipsUnregisteredIds)
    {
        RegisterGlobals(db);

        uint32_t visited = 0;
        const auto count_visit = [&visited](const SystemStatistic &) { visited++; };

        CHECK_FALSE(db.Visit(SystemStatistic::KERNEL_PROCESS_ID, 99, count_visit));
        CHECK_FALSE(db.Visit(SystemStatistic::KERNEL_PROCESS_ID, GAUGE_A_ID + 1000, count_visit));
        CHECK_FALSE(db.Visit(SystemStatistic::KERNEL_PROCESS_ID, 0xFFFFFFFF, count_visit));
        CHECK_FALSE(db.Visit(1, GAUGE_A_ID, count_visit));               //  the same id in a user process
        CHECK_EQUAL(0u, visited);
    }

    TEST(SystemStatisticsDBTests, DuplicateRegistrationRejected)
    {
        RegisterGlobals(db);

        CHECK_DB_RESULT(DUPLICATE_ID, db.Register(gauge_a_stat));
        CHECK_EQUAL(5u, db.Size());
    }

    TEST(SystemStatisticsDBTests, DuplicateIdFromDifferentObjectRejected)
    {
        TestScalarStatistic first(SystemStatistic::KERNEL_PROCESS_ID, 42, StatKind::GAUGE, "g", "first", "desc");
        TestScalarStatistic second(SystemStatistic::KERNEL_PROCESS_ID, 42, StatKind::COUNTER, "g", "second", "desc");

        CHECK_DB_RESULT(SUCCESS, db.Register(first));
        CHECK_DB_RESULT(DUPLICATE_ID, db.Register(second));

        POINTERS_EQUAL(&first, FoundStatistic(db, 42));
        CHECK_EQUAL(1u, db.Size());
    }

    TEST(SystemStatisticsDBTests, SparseIdsAcrossTheWholeRangeAreAccepted)
    {
        constexpr uint32_t MAX_STATIC_ID = 0xFFFFFFFF;

        TestScalarStatistic zero(SystemStatistic::KERNEL_PROCESS_ID, 0, StatKind::GAUGE, "g", "zero", "desc");
        TestScalarStatistic thousand(SystemStatistic::KERNEL_PROCESS_ID, 1000, StatKind::GAUGE, "g", "thousand", "desc");
        TestScalarStatistic max(SystemStatistic::KERNEL_PROCESS_ID, MAX_STATIC_ID, StatKind::GAUGE, "g", "max", "desc");

        CHECK_DB_RESULT(SUCCESS, db.Register(max));
        CHECK_DB_RESULT(SUCCESS, db.Register(zero));
        CHECK_DB_RESULT(SUCCESS, db.Register(thousand));

        CHECK_EQUAL(3u, db.Size());
        POINTERS_EQUAL(&zero, FoundStatistic(db, 0));
        POINTERS_EQUAL(&thousand, FoundStatistic(db, 1000));
        POINTERS_EQUAL(&max, FoundStatistic(db, MAX_STATIC_ID));
    }

    TEST(SystemStatisticsDBTests, RegisterTakesOnlyKernelStatistics)
    {
        TestScalarStatistic kernel(SystemStatistic::KERNEL_PROCESS_ID, 7, StatKind::GAUGE, "g", "kernel", "desc");
        TestScalarStatistic user_process(1, 8, StatKind::GAUGE, "g", "user", "desc");

        CHECK_DB_RESULT(SUCCESS, db.Register(kernel));
        CHECK_DB_RESULT(INVALID_PROCESS_ID, db.Register(user_process));

        CHECK_EQUAL(1u, db.Size());
        CHECK_EQUAL(SystemStatisticsDB::DEFAULT_CAPACITY - 1, db.Available());     //  the refusal kept no room
        POINTERS_EQUAL(&kernel, FoundStatistic(db, 7));
        CHECK_FALSE(db.Contains(1, 8));
    }

    TEST(SystemStatisticsDBTests, RegisterRefusesAKindItsViewDoesNotRepresent)
    {
        //  TestScalarStatistic's kind is chosen at run time, so only registration can catch a non-scalar kind.

        TestScalarStatistic text_kind(SystemStatistic::KERNEL_PROCESS_ID, 7, StatKind::TEXT, "g", "text", "desc");
        TestScalarStatistic blob_kind(SystemStatistic::KERNEL_PROCESS_ID, 8, StatKind::BLOB, "g", "blob", "desc");
        TestScalarStatistic array_kind(SystemStatistic::KERNEL_PROCESS_ID, 9, StatKind::ARRAY, "g", "array", "desc");
        TestScalarStatistic no_kind(SystemStatistic::KERNEL_PROCESS_ID, 10, static_cast<StatKind>(0), "g", "none", "desc");
        TestScalarStatistic probe_kind(SystemStatistic::KERNEL_PROCESS_ID, 11, StatKind::PROBE, "g", "probe", "desc");

        CHECK_DB_RESULT(KIND_NOT_REPRESENTED_BY_VIEW, db.Register(text_kind));
        CHECK_DB_RESULT(KIND_NOT_REPRESENTED_BY_VIEW, db.Register(blob_kind));
        CHECK_DB_RESULT(KIND_NOT_REPRESENTED_BY_VIEW, db.Register(array_kind));
        CHECK_DB_RESULT(KIND_NOT_REPRESENTED_BY_VIEW, db.Register(no_kind));
        CHECK_DB_RESULT(SUCCESS, db.Register(probe_kind));

        CHECK_EQUAL(1u, db.Size());
        CHECK_EQUAL(SystemStatisticsDB::DEFAULT_CAPACITY - 1, db.Available());     //  the refusals kept no room
    }

    TEST(SystemStatisticsDBTests, ForEachVisitsInAscendingIdOrder)
    {
        RegisterGlobals(db);

        uint32_t seen[8] = {};
        uint32_t count   = 0;

        db.ForEach([&](const SystemStatistic &s)
        {
            if (count < 8)
            {
                seen[count] = s.id();
            }
            count++;

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(5u, count);
        CHECK_EQUAL(GAUGE_A_ID,     seen[0]);
        CHECK_EQUAL(COUNTER_A_ID,   seen[1]);
        CHECK_EQUAL(GAUGE_B_ID,     seen[2]);
        CHECK_EQUAL(COUNTER_B_ID,   seen[3]);
        CHECK_EQUAL(TIMESTAMP_A_ID, seen[4]);
    }

    TEST(SystemStatisticsDBTests, ForEachStopsWhenTheCallbackSaysSo)
    {
        RegisterGlobals(db);

        uint32_t visited = 0;

        db.ForEach([&visited](const SystemStatistic &)
        {
            visited++;
            return (visited == 2) ? SystemStatisticsDB::VisitAction::STOP : SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL(2u, visited);

        const SystemStatistic *first_counter = nullptr;

        db.ForEach([&first_counter](const SystemStatistic &stat)
        {
            if (stat.Kind() != StatKind::COUNTER)
            {
                return SystemStatisticsDB::VisitAction::CONTINUE;
            }

            first_counter = &stat;
            return SystemStatisticsDB::VisitAction::STOP;
        });

        POINTERS_EQUAL(&counter_a_stat, first_counter);         //  the lowest-id counter

        visited = 0;
        db.ForEach([&visited](const SystemStatistic &) { visited++; return SystemStatisticsDB::VisitAction::CONTINUE; });
        CHECK_EQUAL(5u, visited);
    }

    TEST(SystemStatisticsDBTests, ValuesReachableThroughDbLookup)
    {
        RegisterGlobals(db);

        //  Production pattern: subsystem writes its own global; query side
        //  reads through the DB's const pointer.  Verify both paths agree.

        gauge_a_stat.Set(4242);
        const auto *found_gauge = static_cast<const GaugeStatistic<GAUGE_A_ID> *>(FoundStatistic(db, GAUGE_A_ID));
        CHECK(found_gauge != nullptr);
        CHECK_EQUAL((int64_t)4242, found_gauge->Get());

        //  Delta-based so the test is independent of prior increments.

        const int64_t value_before = counter_a_stat.Get();
        counter_a_stat.Increment(5);
        const auto *found_counter = static_cast<const CounterStatistic<COUNTER_A_ID> *>(FoundStatistic(db, COUNTER_A_ID));
        CHECK(found_counter != nullptr);
        CHECK_EQUAL(value_before + 5, found_counter->Get());
    }

    TEST(SystemStatisticsDBTests, SchemaReturnsAllInAscendingIdOrder)
    {
        RegisterGlobals(db);

        auto schema_result = db.Schema();
        CHECK_TRUE(schema_result.Successful());
        const SystemStatisticsSchema &schema = *schema_result.Value();
        CHECK_EQUAL(5u, schema.Count());
        CHECK_EQUAL(GAUGE_A_ID,     schema[0].id());
        CHECK_EQUAL(COUNTER_A_ID,   schema[1].id());
        CHECK_EQUAL(GAUGE_B_ID,     schema[2].id());
        CHECK_EQUAL(COUNTER_B_ID,   schema[3].id());
        CHECK_EQUAL(TIMESTAMP_A_ID, schema[4].id());

        for (uint32_t i = 0; i < schema.Count(); i++)
        {
            const SystemStatistic* registered = FoundStatistic(db, schema[i].id());
            CHECK(registered != nullptr);
            CHECK_EQUAL(static_cast<int>(registered->Kind()), static_cast<int>(schema[i].Kind()));
            STRCMP_EQUAL(registered->Group(), schema[i].Group());
            STRCMP_EQUAL(registered->Name(),  schema[i].Name());
            STRCMP_EQUAL(registered->Description(), schema[i].Description());
        }
    }

    TEST(SystemStatisticsDBTests, NullMetadataReadsAsEmptyString)
    {
        GaugeStatistic<1100> unnamed(nullptr, nullptr, nullptr, 0);

        STRCMP_EQUAL("", unnamed.Group());
        STRCMP_EQUAL("", unnamed.Name());
        STRCMP_EQUAL("", unnamed.Description());

        TestScalarStatistic unnamed_type(SystemStatistic::KERNEL_PROCESS_ID, 1101, StatKind::GAUGE, nullptr, nullptr, nullptr);

        STRCMP_EQUAL("", unnamed_type.Group());
        STRCMP_EQUAL("", unnamed_type.Name());
        STRCMP_EQUAL("", unnamed_type.Description());

        SystemStatisticsDB unnamed_db;
        CHECK_DB_RESULT(SUCCESS, unnamed_db.Initialize(stats_db_test_resource));
        CHECK_DB_RESULT(SUCCESS, unnamed_db.Register(unnamed));

        auto schema_result = unnamed_db.Schema();               //  copies the metadata: crashed on nullptr before
        CHECK_TRUE(schema_result.Successful());
        const SystemStatisticsSchema &schema = *schema_result.Value();
        CHECK_EQUAL(1u, schema.Count());
        STRCMP_EQUAL("", schema[0].Group());
        STRCMP_EQUAL("", schema[0].Name());
        STRCMP_EQUAL("", schema[0].Description());
    }

    TEST(SystemStatisticsDBTests, SchemaOnEmptyDbReturnsEmptySchema)
    {
        auto schema_result = db.Schema();
        CHECK_TRUE(schema_result.Successful());
        const SystemStatisticsSchema &schema = *schema_result.Value();
        CHECK_EQUAL(0u, schema.Count());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(TimestampStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(TimestampStatisticTests, InitialValueIsZeroAndNeverOccurred)
    {
        TimestampStatistic<30> ts("memory", "last event", "desc");

        CHECK_EQUAL((int64_t)0, ts.Get());
        CHECK_FALSE(ts.HasOccurred());
    }

    TEST(TimestampStatisticTests, StampWithExplicitTicksStoresValue)
    {
        TimestampStatistic<30> ts("memory", "last event", "desc");

        ts.Stamp(12345);
        CHECK_EQUAL((int64_t)12345, ts.Get());
        CHECK_TRUE(ts.HasOccurred());
    }

    TEST(TimestampStatisticTests, StampWithZeroMeansNever)
    {
        TimestampStatistic<30> ts("memory", "last event", "desc");
        ts.Stamp(999);
        CHECK_TRUE(ts.HasOccurred());

        ts.Stamp(0);
        CHECK_EQUAL((int64_t)0, ts.Get());
        CHECK_FALSE(ts.HasOccurred());
    }

    TEST(TimestampStatisticTests, LastWriterWins)
    {
        TimestampStatistic<30> ts("memory", "last event", "desc");

        ts.Stamp(100);
        ts.Stamp(200);
        ts.Stamp(150);
        CHECK_EQUAL((int64_t)150, ts.Get());
    }

    TEST(TimestampStatisticTests, NoArgStampUsesTickSource)
    {
        static int64_t fake_ticks;
        fake_ticks = 7777;                                      //  reset on every run: -r repeats the test
        TimestampTickSource::SetTickFunction([]() -> int64_t { return fake_ticks; });

        TimestampStatistic<30> ts("memory", "last event", "desc");
        ts.Stamp();
        CHECK_EQUAL((int64_t)7777, ts.Get());
        CHECK_TRUE(ts.HasOccurred());

        fake_ticks = 9999;
        ts.Stamp();
        CHECK_EQUAL((int64_t)9999, ts.Get());

        TimestampTickSource::SetTickFunction(nullptr);   // restore default
    }

    TEST(TimestampStatisticTests, AsTimePointBeforeFirstStampIsEpochZero)
    {
        TimestampTickSource::SetTickFrequency(1000000);  //  1 MHz

        TimestampStatistic<30> ts("memory", "last event", "desc");
        CHECK_EQUAL(0, minstd::chrono::duration_cast<minstd::chrono::nanoseconds>(ts.AsTimePoint().time_since_epoch()).count());
    }

    TEST(TimestampStatisticTests, AsTimePointConvertsTicksToNanoseconds)
    {
        //  At 1 kHz one tick is one millisecond, so three ticks are three milliseconds.
        TimestampTickSource::SetTickFrequency(1000);

        TimestampStatistic<30> ts("memory", "last event", "desc");
        ts.Stamp(3);
        CHECK_EQUAL(3000000, minstd::chrono::duration_cast<minstd::chrono::nanoseconds>(ts.AsTimePoint().time_since_epoch()).count());
        CHECK_EQUAL(3, minstd::chrono::duration_cast<minstd::chrono::milliseconds>(ts.AsTimePoint().time_since_epoch()).count());
    }

    TEST(TimestampStatisticTests, AsTimePointStaysExactForLongUptimes)
    {
        //  An hour of 54 MHz ticks is well past where ticks * 1e9 overflows 64 bits.
        TimestampTickSource::SetTickFrequency(54000000);

        TimestampStatistic<30> ts("memory", "last event", "desc");
        ts.Stamp(3600LL * 54000000LL);
        CHECK_EQUAL(3600, minstd::chrono::duration_cast<minstd::chrono::seconds>(ts.AsTimePoint().time_since_epoch()).count());

        TimestampTickSource::SetTickFrequency(0);
    }

    TEST(TimestampStatisticTests, AsTimePointWithUnknownFrequencyIsEpochZero)
    {
        TimestampTickSource::SetTickFrequency(0);        //  host default: frequency unknown

        TimestampStatistic<30> ts("memory", "last event", "desc");
        ts.Stamp(12345);
        CHECK_EQUAL((int64_t)12345, ts.Get());           //  raw ticks still readable
        CHECK_EQUAL(0, minstd::chrono::duration_cast<minstd::chrono::nanoseconds>(ts.AsTimePoint().time_since_epoch()).count());
    }

    TEST(TimestampStatisticTests, KindAndRepresentationReportTimestamp)
    {
        CHECK_EQUAL(static_cast<int>(StatKind::TIMESTAMP), static_cast<int>(timestamp_a_stat.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(timestamp_a_stat.Representation()));
    }

    TEST(TimestampStatisticTests, MetadataExposedThroughBasePointer)
    {
        const SystemStatistic &base = timestamp_a_stat;

        CHECK_EQUAL(TIMESTAMP_A_ID, base.id());
        STRCMP_EQUAL("memory", base.Group());
        STRCMP_EQUAL("last invalid release", base.Name());
        STRCMP_EQUAL("Time of most recent invalid release", base.Description());
    }

    TEST(TimestampStatisticTests, ValueReachableThroughDbLookup)
    {
        timestamp_a_stat.Stamp(4242);
        const auto *ts = static_cast<const TimestampStatistic<TIMESTAMP_A_ID> *>(FoundStatistic(GetSystemStatisticsDB(), TIMESTAMP_A_ID));
        CHECK(ts != nullptr);
        CHECK_EQUAL((int64_t)4242, ts->Get());
        CHECK_TRUE(ts->HasOccurred());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(TextStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(TextStatisticTests, EmptyUntilTheFirstSet)
    {
        TextStatistic<50, 32> ts("kernel", "version", "Current kernel version string");
        char buf[33] = {'x'};

        CHECK_FALSE(ts.HasValue());
        CHECK_EQUAL(0u, ts.Length());
        CHECK_RESULT(EMPTY, ts.Get(buf, sizeof(buf)));
        STRCMP_EQUAL("", buf);
    }

    TEST(TextStatisticTests, SetAndGetRoundtrip)
    {
        TextStatistic<51, 32> ts("kernel", "version", "Current kernel version string");
        char buf[33]{};

        CHECK_RESULT(SUCCESS, ts.Set("v1.0.0-alpha"));
        CHECK_TRUE(ts.HasValue());
        CHECK_EQUAL(12u, ts.Length());
        CHECK_RESULT(SUCCESS, ts.Get(buf, sizeof(buf)));
        STRCMP_EQUAL("v1.0.0-alpha", buf);
    }

    TEST(TextStatisticTests, MultipleSetsCycleSlots)
    {
        TextStatistic<52, 32> ts("kernel", "state", "Current kernel state");
        char buf[33]{};

        ts.Set("booting");
        ts.Set("running");
        ts.Set("idle");
        CHECK_RESULT(SUCCESS, ts.Get(buf, sizeof(buf)));
        STRCMP_EQUAL("idle", buf);
    }

    TEST(TextStatisticTests, TextLongerThanMaxLenIsTruncated)
    {
        TextStatistic<53, 8> ts("kernel", "short", "Short stat");
        char buf[9]{};

        CHECK_RESULT(SUCCESS, ts.Set("exactly8"));               //  exactly MaxLen
        CHECK_RESULT(TRUNCATED, ts.Set("toolongtext"));          //  11 chars, max 8
        CHECK_RESULT(SUCCESS, ts.Get(buf, sizeof(buf)));
        STRCMP_EQUAL("toolongt", buf);
    }

    TEST(TextStatisticTests, GetIntoASmallBufferIsTruncated)
    {
        TextStatistic<57, 32> ts("kernel", "version", "desc");
        char buf[4]{};

        ts.Set("abcdef");
        CHECK_RESULT(TRUNCATED, ts.Get(buf, sizeof(buf)));
        STRCMP_EQUAL("abc", buf);                               //  still null-terminated
    }

    TEST(TextStatisticTests, SetNullptrStoresTheEmptyString)
    {
        TextStatistic<58, 32> ts("kernel", "version", "desc");
        char buf[8] = {'x'};

        ts.Set("abc");
        CHECK_RESULT(SUCCESS, ts.Set(nullptr));
        CHECK_TRUE(ts.HasValue());
        CHECK_RESULT(SUCCESS, ts.Get(buf, sizeof(buf)));
        STRCMP_EQUAL("", buf);
    }

    TEST(TextStatisticTests, SlotCountExhaustsAndWrapsCorrectly)
    {
        TextStatistic<54, 16, 2> ts("kernel", "status", "Two-slot stat");
        char buf[17]{};

        for (uint32_t i = 0; i < 7; i++)        // 7 writes: last index is 6 (even)
        {
            CHECK_RESULT(SUCCESS, ts.Set((i % 2 == 0) ? "even" : "odd"));
        }
        ts.Get(buf, sizeof(buf));
        STRCMP_EQUAL("even", buf);
    }

    TEST(TextStatisticTests, KindAndRepresentationReportText)
    {
        TextStatistic<55, 32> ts("kernel", "version", "desc");
        CHECK_EQUAL(static_cast<int>(StatKind::TEXT), static_cast<int>(ts.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::TEXT), static_cast<int>(ts.Representation()));
    }

    TEST(TextStatisticTests, MetadataExposedThroughBasePointer)
    {
        TextStatistic<56, 32> ts("kernel", "version", "Current kernel version string");
        const SystemStatistic &base = ts;

        CHECK_EQUAL(56u, base.id());
        STRCMP_EQUAL("kernel",                        base.Group());
        STRCMP_EQUAL("version",                       base.Name());
        STRCMP_EQUAL("Current kernel version string", base.Description());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(BlobStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(BlobStatisticTests, EmptyUntilTheFirstSet)
    {
        BlobStatistic<60, 8> bs("hw", "registers", "Raw register snapshot");
        uint8_t buf[8]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        CHECK_FALSE(bs.HasValue());
        CHECK_EQUAL(0u, bs.Size());
        CHECK_RESULT(EMPTY, bs.Get(buf, sizeof(buf)));
        for (uint32_t i = 0; i < 8; i++)
        {
            CHECK_EQUAL(0xFF, buf[i]);                          //  nothing copied
        }
    }

    TEST(BlobStatisticTests, SetAndGetRoundtrip)
    {
        BlobStatistic<61, 4> bs("hw", "registers", "Raw register snapshot");
        const uint8_t src[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        uint8_t       dst[4]{};

        CHECK_RESULT(SUCCESS, bs.Set(src, sizeof(src)));
        CHECK_TRUE(bs.HasValue());
        CHECK_EQUAL(4u, bs.Size());
        CHECK_RESULT(SUCCESS, bs.Get(dst, sizeof(dst)));
        CHECK_EQUAL(0, memcmp(src, dst, 4));
    }

    TEST(BlobStatisticTests, VariableLengthRoundTrips)
    {
        BlobStatistic<66, 8> bs("hw", "registers", "desc");
        const uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        uint8_t       dst[8]{};

        CHECK_RESULT(SUCCESS, bs.Set(src, 3));
        CHECK_EQUAL(3u, bs.Size());
        CHECK_RESULT(SUCCESS, bs.Get(dst, sizeof(dst)));
        CHECK_EQUAL(0, memcmp(src, dst, 3));
        CHECK_EQUAL(0, dst[3]);                                 //  only Size() bytes copied

        CHECK_RESULT(SUCCESS, bs.Set(nullptr, 0));
        CHECK_EQUAL(0u, bs.Size());
        CHECK_RESULT(SUCCESS, bs.Get(dst, sizeof(dst)));        //  a stored empty blob is not EMPTY
    }

    TEST(BlobStatisticTests, OversizeSetAndUndersizeGetAreTruncated)
    {
        BlobStatistic<67, 4> bs("hw", "registers", "desc");
        const uint8_t src[6] = {1, 2, 3, 4, 5, 6};
        uint8_t       dst[4]{};

        CHECK_RESULT(TRUNCATED, bs.Set(src, sizeof(src)));
        CHECK_EQUAL(4u, bs.Size());
        CHECK_RESULT(TRUNCATED, bs.Get(dst, 2));
        CHECK_EQUAL(1, dst[0]);
        CHECK_EQUAL(2, dst[1]);
        CHECK_EQUAL(0, dst[2]);
    }

    TEST(BlobStatisticTests, MultipleSetsCycleSlots)
    {
        BlobStatistic<62, 4> bs("hw", "registers", "Raw register snapshot");
        const uint8_t a[4] = {0x01, 0x02, 0x03, 0x04};
        const uint8_t b[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        uint8_t       dst[4]{};

        bs.Set(a, sizeof(a));
        bs.Set(b, sizeof(b));
        bs.Set(a, sizeof(a));
        bs.Get(dst, sizeof(dst));
        CHECK_EQUAL(0, memcmp(a, dst, 4));
    }

    TEST(BlobStatisticTests, SlotWrapAroundPreservesLastWrite)
    {
        BlobStatistic<63, 2, 2> bs("hw", "status", "Two-slot blob");
        uint8_t dst[2]{};

        for (uint32_t i = 0; i < 7; i++)        // 7 writes: last index is 6 (even slot)
        {
            const uint8_t v[2] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i + 1)};
            CHECK_RESULT(SUCCESS, bs.Set(v, sizeof(v)));
        }

        const uint8_t expected[2] = {6, 7};
        bs.Get(dst, sizeof(dst));
        CHECK_EQUAL(0, memcmp(expected, dst, 2));
    }

    TEST(BlobStatisticTests, KindAndRepresentationReportBlob)
    {
        BlobStatistic<64, 8> bs("hw", "registers", "desc");
        CHECK_EQUAL(static_cast<int>(StatKind::BLOB), static_cast<int>(bs.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::BLOB), static_cast<int>(bs.Representation()));
    }

    TEST(BlobStatisticTests, MetadataExposedThroughBasePointer)
    {
        BlobStatistic<65, 8> bs("hw", "registers", "Raw register snapshot");
        const SystemStatistic &base = bs;

        CHECK_EQUAL(65u, base.id());
        STRCMP_EQUAL("hw",                    base.Group());
        STRCMP_EQUAL("registers",             base.Name());
        STRCMP_EQUAL("Raw register snapshot", base.Description());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(ProbeStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(ProbeStatisticTests, GetCallsProbeFunction)
    {
        ProbeStatistic<70> ps("cpu", "load", "CPU load estimate", []() -> int64_t { return 42; });
        CHECK_EQUAL((int64_t)42, ps.Get());
    }

    TEST(ProbeStatisticTests, GetReflectsCurrentComputedValue)
    {
        static int64_t value;
        value = 100;                                            //  reset on every run: -r repeats the test
        ProbeStatistic<71> ps("cpu", "load", "CPU load estimate", []() -> int64_t { return value; });

        CHECK_EQUAL((int64_t)100, ps.Get());
        value = 200;
        CHECK_EQUAL((int64_t)200, ps.Get());
        value = 0;
        CHECK_EQUAL((int64_t)0, ps.Get());
    }

    TEST(ProbeStatisticTests, NullProbeFunctionReturnsZero)
    {
        ProbeStatistic<72> ps("cpu", "load", "CPU load estimate", nullptr);
        CHECK_EQUAL((int64_t)0, ps.Get());
    }

    TEST(ProbeStatisticTests, KindAndRepresentationReportProbe)
    {
        ProbeStatistic<73> ps("cpu", "load", "desc", []() -> int64_t { return 1; });
        CHECK_EQUAL(static_cast<int>(StatKind::PROBE), static_cast<int>(ps.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(ps.Representation()));
    }

    TEST(ProbeStatisticTests, MetadataExposedThroughBasePointer)
    {
        ProbeStatistic<74> ps("cpu", "load", "CPU load estimate", []() -> int64_t { return 0; });
        const SystemStatistic &base = ps;

        CHECK_EQUAL(74u, base.id());
        STRCMP_EQUAL("cpu",               base.Group());
        STRCMP_EQUAL("load",              base.Name());
        STRCMP_EQUAL("CPU load estimate", base.Description());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(GaugeArrayStatisticTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(GaugeArrayStatisticTests, AllElementsStartAtTheInitialValue)
    {
        GaugeArrayStatistic<90, 4> array_stat("tasks", "runlist_depth", "desc", 5);

        CHECK_EQUAL(4u, array_stat.Count());

        for (uint32_t i = 0; i < 4; i++)
        {
            CHECK_EQUAL((int64_t)5, *array_stat.GetValue<int64_t>(i));
        }
    }

    TEST(GaugeArrayStatisticTests, IncrementMovesOnlyItsOwnElement)
    {
        GaugeArrayStatistic<91, 4> array_stat("tasks", "runlist_depth", "desc", 0);

        array_stat.Increment(1, 3);
        array_stat.Increment(1, -1);
        array_stat.Increment(3);

        CHECK_EQUAL((int64_t)0, *array_stat.GetValue<int64_t>(0));
        CHECK_EQUAL((int64_t)2, *array_stat.GetValue<int64_t>(1));   //  +3 then -1
        CHECK_EQUAL((int64_t)0, *array_stat.GetValue<int64_t>(2));
        CHECK_EQUAL((int64_t)1, *array_stat.GetValue<int64_t>(3));
    }

    TEST(GaugeArrayStatisticTests, IncrementReturnsPreviousValue)
    {
        GaugeArrayStatistic<92, 2> array_stat("g", "array", "desc", 100);

        const auto previous = array_stat.Increment(0, 5);

        CHECK_TRUE(previous.Successful());
        CHECK_EQUAL((int64_t)100, *previous);
        CHECK_EQUAL((int64_t)105, *array_stat.GetValue<int64_t>(0));
    }

    TEST(GaugeArrayStatisticTests, SetReplacesAnElement)
    {
        GaugeArrayStatistic<93, 2> array_stat("g", "array", "desc", 0);

        array_stat.Set(1, -12345);
        CHECK_EQUAL((int64_t)-12345, *array_stat.GetValue<int64_t>(1));
    }

    TEST(GaugeArrayStatisticTests, OutOfRangeIndexIsReportedOrIgnoredButNeverReadAsAValue)
    {
        GaugeArrayStatistic<94, 2> array_stat("g", "array", "desc", 7);

        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(array_stat.Increment(2, 5).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(array_stat.Increment(0xFFFFFFFF, 5).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(array_stat.GetValue<int64_t>(2).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(array_stat.GetValue<int64_t>(0xFFFFFFFF).ResultCode()));

        array_stat.Set(2, 99);                          //  silently dropped

        //  the in-range elements are untouched
        CHECK_EQUAL((int64_t)7, *array_stat.GetValue<int64_t>(0));
        CHECK_EQUAL((int64_t)7, *array_stat.GetValue<int64_t>(1));
    }

    TEST(GaugeArrayStatisticTests, OnlyTheCountedElementsExist)
    {
        GaugeArrayStatistic<88, 8> array_stat("g", "array", "desc", 5, 3);
        const ArrayStatisticView &view = array_stat;

        CHECK_EQUAL(8u, (GaugeArrayStatistic<88, 8>::CAPACITY));
        CHECK_EQUAL(3u, array_stat.Count());
        CHECK_EQUAL(3u, view.Count());

        for (uint32_t i = 0; i < 3; i++)
        {
            CHECK_EQUAL((int64_t)5, *view.GetValue<int64_t>(i));
        }

        //  Elements 3 to 7 are room, not elements: every operation treats them as past the end.

        array_stat.Set(3, 99);

        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(array_stat.Increment(3).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(view.GetValue<int64_t>(3).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(view.GetValue<int64_t>(7).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE),
                    static_cast<int>(static_cast<const SystemStatistic &>(array_stat).GetValue<int64_t>(3).ResultCode()));
    }

    TEST(GaugeArrayStatisticTests, CountDefaultsToAndIsClampedToCapacity)
    {
        GaugeArrayStatistic<87, 4> by_default("g", "array", "desc", 0);
        GaugeArrayStatistic<86, 4> too_many("g", "array", "desc", 0, 5);
        GaugeArrayStatistic<85, 4> none("g", "array", "desc", 0, 0);

        CHECK_EQUAL(4u, by_default.Count());
        CHECK_EQUAL(4u, too_many.Count());
        CHECK_EQUAL(0u, none.Count());

        CHECK_EQUAL((int64_t)0, *too_many.GetValue<int64_t>(3));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(too_many.GetValue<int64_t>(4).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(none.GetValue<int64_t>(0).ResultCode()));
    }

    TEST(GaugeArrayStatisticTests, NegativeElementsAreValues)
    {
        //  A gauge may go negative, so no value can stand for "no element": -1 and 0 read back as themselves.

        GaugeArrayStatistic<89, 2> array_stat("g", "array", "desc", -1);

        const auto previous = array_stat.Increment(1, 1);

        CHECK_TRUE(previous.Successful());
        CHECK_EQUAL((int64_t)-1, *previous);
        CHECK_EQUAL((int64_t)-1, *array_stat.GetValue<int64_t>(0));
        CHECK_EQUAL((int64_t)0, *array_stat.GetValue<int64_t>(1));
    }

    TEST(GaugeArrayStatisticTests, KindAndRepresentationReportArray)
    {
        GaugeArrayStatistic<95, 4> array_stat("g", "array", "desc", 0);

        CHECK_EQUAL(static_cast<int>(StatKind::ARRAY), static_cast<int>(array_stat.Kind()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::ARRAY), static_cast<int>(array_stat.Representation()));
    }

    TEST(GaugeArrayStatisticTests, ElementsReachableThroughTheBaseClass)
    {
        GaugeArrayStatistic<96, 4> array_stat("g", "array", "desc", 0);

        array_stat.Increment(2, 9);

        const SystemStatistic &base = array_stat;

        CHECK_EQUAL(96u, base.id());
        STRCMP_EQUAL("g", base.Group());
        STRCMP_EQUAL("array", base.Name());

        const auto element = base.GetValue<int64_t>(2);
        CHECK_TRUE(element.Successful());
        CHECK_EQUAL((int64_t)9, *element);

        CHECK_EQUAL((int64_t)0, *base.GetValue<int64_t>(0));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(base.GetValue<int64_t>(4).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(base.GetValue<int64_t>(0xFFFFFFFF).ResultCode()));

        const ArrayStatisticView *array = base.As<ArrayStatisticView>();
        POINTERS_EQUAL(&array_stat, array);
        CHECK_EQUAL(4u, array->Count());
        CHECK_EQUAL((int64_t)9, *array->GetValue<int64_t>(2));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INDEX_OUT_OF_RANGE), static_cast<int>(array->GetValue<int64_t>(4).ResultCode()));
    }

    TEST(GaugeArrayStatisticTests, AnArrayHasNoSingleValue)
    {
        GaugeArrayStatistic<97, 2> array_stat("g", "array", "desc", 3);
        const SystemStatistic &base = array_stat;

        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INVALID_VALUE_TYPE), static_cast<int>(base.GetValue<int64_t>().ResultCode()));
        POINTERS_EQUAL(nullptr, base.As<ScalarStatisticView>());
    }

    TEST(GaugeArrayStatisticTests, NonArrayStatisticsHaveNoElements)
    {
        GaugeStatistic<98> gauge("g", "gauge", "desc", 0);
        TextStatistic<99, 8> text("g", "text", "desc");
        const SystemStatistic &gauge_base = gauge;
        const SystemStatistic &text_base = text;

        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INVALID_VALUE_TYPE), static_cast<int>(gauge_base.GetValue<int64_t>(0).ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INVALID_VALUE_TYPE), static_cast<int>(text_base.GetValue<int64_t>(0).ResultCode()));
        POINTERS_EQUAL(nullptr, gauge_base.As<ArrayStatisticView>());
        POINTERS_EQUAL(nullptr, text_base.As<ArrayStatisticView>());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(StatisticValueTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(StatisticValueTests, ScalarKindsReportTheirCurrentValueThroughTheBaseClass)
    {
        GaugeStatistic<80> gauge("g", "gauge", "desc", 0);
        CounterStatistic<81> counter("g", "counter", "desc", 10);
        TimestampStatistic<82> timestamp("g", "timestamp", "desc");
        ProbeStatistic<83> probe("g", "probe", "desc", []() -> int64_t { return -77; });

        gauge.Set(-5);
        counter.Increment(3);
        timestamp.Stamp(123456);

        const auto gauge_value = static_cast<const SystemStatistic &>(gauge).GetValue<int64_t>();
        CHECK_TRUE(gauge_value.Successful());
        CHECK_EQUAL((int64_t)-5, *gauge_value);

        const auto counter_value = static_cast<const SystemStatistic &>(counter).GetValue<int64_t>();
        CHECK_TRUE(counter_value.Successful());
        CHECK_EQUAL((int64_t)13, *counter_value);

        const auto timestamp_value = static_cast<const SystemStatistic &>(timestamp).GetValue<int64_t>();
        CHECK_TRUE(timestamp_value.Successful());
        CHECK_EQUAL((int64_t)123456, *timestamp_value);

        const auto probe_value = static_cast<const SystemStatistic &>(probe).GetValue<int64_t>();
        CHECK_TRUE(probe_value.Successful());
        CHECK_EQUAL((int64_t)-77, *probe_value);
    }

    TEST(StatisticValueTests, TextAndBlobHaveNoScalarValue)
    {
        TextStatistic<84, 16> text("g", "text", "desc");
        BlobStatistic<85, 8> blob("g", "blob", "desc");

        text.Set("hello");
        blob.Set("abc", 3);

        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INVALID_VALUE_TYPE),
                    static_cast<int>(static_cast<const SystemStatistic &>(text).GetValue<int64_t>().ResultCode()));
        CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::INVALID_VALUE_TYPE),
                    static_cast<int>(static_cast<const SystemStatistic &>(blob).GetValue<int64_t>().ResultCode()));
    }

    TEST(StatisticValueTests, ConcreteAndViewTypesReadWithoutARunTimeCheck)
    {
        GaugeStatistic<86> gauge("g", "gauge", "desc", 21);
        const ScalarStatisticView &view = gauge;

        CHECK_EQUAL((int64_t)21, *gauge.GetValue<int64_t>());
        CHECK_EQUAL((int64_t)21, *view.GetValue<int64_t>());
        CHECK_EQUAL((int64_t)21, view.Get());
    }

    TEST(StatisticValueTests, EveryKindHasItsOwnName)
    {
        STRCMP_EQUAL("gauge", ToString(StatKind::GAUGE));
        STRCMP_EQUAL("counter", ToString(StatKind::COUNTER));
        STRCMP_EQUAL("timestamp", ToString(StatKind::TIMESTAMP));
        STRCMP_EQUAL("text", ToString(StatKind::TEXT));
        STRCMP_EQUAL("blob", ToString(StatKind::BLOB));
        STRCMP_EQUAL("probe", ToString(StatKind::PROBE));
        STRCMP_EQUAL("array", ToString(StatKind::ARRAY));
        STRCMP_EQUAL("unknown", ToString(static_cast<StatKind>(0)));
    }

    TEST(StatisticValueTests, KernelStatisticIdsAreTheTemplateIds)
    {
        CounterStatistic<KernelStatistic(KernelStatisticId::USER_TASK_FAULTS)> faults("tasks", "user_faults", "desc", 0);

        CHECK_EQUAL(static_cast<uint32_t>(KernelStatisticId::USER_TASK_FAULTS), faults.id());
        CHECK_EQUAL(SystemStatistic::KERNEL_PROCESS_ID, faults.ProcessId());
    }

    // =========================================================================
    //  Views: what each statistic can be read as, checked at compile time
    //      where the type is known and at run time where it is not.
    // =========================================================================

    //  Whether a call compiles, for the static_asserts below: a constraint that fails, or a deleted overload,
    //      makes the expression ill-formed and the concept false.

    template<typename Statistic, typename T>
    concept CanGetValue = requires(const Statistic &stat) { stat.template GetValue<T>(); };

    template<typename Statistic, typename T>
    concept CanGetElement = requires(const Statistic &stat) { stat.template GetValue<T>(0u); };

    template<typename Statistic, typename View>
    concept CanViewAs = requires(const Statistic &stat) { stat.template As<View>(); };

    template<typename Statistic>
    concept CanRegister = requires(SystemStatisticsDB &db, const Statistic &stat) { db.Register(stat); };

    template<typename Statistic>
    concept CanRegisterDynamic = requires(SystemStatisticsDB &db, Statistic &stat) { db.RegisterDynamic(stat); };

    using CompileTimeGauge = GaugeStatistic<1200>;
    using CompileTimeText  = TextStatistic<1201, 8>;
    using CompileTimeBlob  = BlobStatistic<1202, 8>;
    using CompileTimeArray = GaugeArrayStatistic<1203, 2>;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(StatisticViewCompileTimeTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    //  Every check here is a static_assert: the test exists to name the guarantee, and passes once it compiles.

    TEST(StatisticViewCompileTimeTests, AConcreteStatisticAcceptsOnlyItsOwnValueType)
    {
        static_assert(CanGetValue<CompileTimeGauge, int64_t>);
        static_assert(!CanGetValue<CompileTimeGauge, int32_t>);
        static_assert(!CanGetValue<CompileTimeGauge, uint64_t>);
        static_assert(!CanGetElement<CompileTimeGauge, int64_t>);

        static_assert(!CanGetValue<CompileTimeText, int64_t>);
        static_assert(!CanGetElement<CompileTimeText, int64_t>);

        static_assert(!CanGetValue<CompileTimeBlob, int64_t>);
        static_assert(!CanGetElement<CompileTimeBlob, int64_t>);

        static_assert(!CanGetValue<CompileTimeArray, int64_t>);
        static_assert(CanGetElement<CompileTimeArray, int64_t>);
        static_assert(!CanGetElement<CompileTimeArray, int32_t>);
    }

    TEST(StatisticViewCompileTimeTests, AViewAcceptsOnlyItsOwnValueType)
    {
        static_assert(CanGetValue<ScalarStatisticView, int64_t>);
        static_assert(!CanGetValue<ScalarStatisticView, int32_t>);
        static_assert(!CanGetElement<ScalarStatisticView, int64_t>);

        static_assert(!CanGetValue<TextStatisticView, int64_t>);
        static_assert(!CanGetValue<BlobStatisticView, int64_t>);

        static_assert(!CanGetValue<ArrayStatisticView, int64_t>);
        static_assert(CanGetElement<ArrayStatisticView, int64_t>);
    }

    TEST(StatisticViewCompileTimeTests, TheBaseClassAcceptsOnlyTypesSomeStatisticHas)
    {
        static_assert(CanGetValue<SystemStatistic, int64_t>);
        static_assert(!CanGetValue<SystemStatistic, int32_t>);
        static_assert(!CanGetValue<SystemStatistic, uint64_t>);
        static_assert(!CanGetValue<SystemStatistic, double>);
        static_assert(!CanGetValue<SystemStatistic, const char *>);

        static_assert(CanGetElement<SystemStatistic, int64_t>);
        static_assert(!CanGetElement<SystemStatistic, int32_t>);
    }

    TEST(StatisticViewCompileTimeTests, OnlyTheFourViewsCanBeAskedFor)
    {
        static_assert(CanViewAs<SystemStatistic, ScalarStatisticView>);
        static_assert(CanViewAs<SystemStatistic, TextStatisticView>);
        static_assert(CanViewAs<SystemStatistic, BlobStatisticView>);
        static_assert(CanViewAs<SystemStatistic, ArrayStatisticView>);

        static_assert(!CanViewAs<SystemStatistic, SystemStatistic>);
        static_assert(!CanViewAs<SystemStatistic, CompileTimeGauge>);       //  a concrete class: its representation does not identify it
        static_assert(!CanViewAs<SystemStatistic, int64_t>);
    }

    TEST(StatisticViewCompileTimeTests, EachStatisticKindMatchesItsView)
    {
        static_assert(RepresentationOf(GaugeStatistic<1>::KIND) == ScalarStatisticView::REPRESENTATION);
        static_assert(RepresentationOf(CounterStatistic<1>::KIND) == ScalarStatisticView::REPRESENTATION);
        static_assert(RepresentationOf(TimestampStatistic<1>::KIND) == ScalarStatisticView::REPRESENTATION);
        static_assert(RepresentationOf(ProbeStatistic<1>::KIND) == ScalarStatisticView::REPRESENTATION);
        static_assert(RepresentationOf(TextStatistic<1, 8>::KIND) == TextStatisticView::REPRESENTATION);
        static_assert(RepresentationOf(BlobStatistic<1, 8>::KIND) == BlobStatisticView::REPRESENTATION);
        static_assert(RepresentationOf(GaugeArrayStatistic<1, 2>::KIND) == ArrayStatisticView::REPRESENTATION);

        static_assert(minstd::derived_from<CompileTimeGauge, ScalarStatisticView>);
        static_assert(minstd::derived_from<CompileTimeText, TextStatisticView>);
        static_assert(minstd::derived_from<CompileTimeBlob, BlobStatisticView>);
        static_assert(minstd::derived_from<CompileTimeArray, ArrayStatisticView>);
    }

    TEST(StatisticViewCompileTimeTests, OnlyStatisticsCanBeRegistered)
    {
        static_assert(CanRegister<CompileTimeGauge>);
        static_assert(CanRegister<TestScalarStatistic>);
        static_assert(!CanRegister<SystemStatisticType>);                  //  metadata only: it has no value to read

        static_assert(!CanRegisterDynamic<CompileTimeGauge>);              //  compile-time statistics are never removable
        static_assert(!CanRegisterDynamic<TestScalarStatistic>);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(StatisticViewTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(StatisticViewTests, EachStatisticIsReachableThroughItsViewOnly)
    {
        GaugeStatistic<1210> gauge("g", "gauge", "desc", 0);
        TextStatistic<1211, 8> text("g", "text", "desc");
        BlobStatistic<1212, 8> blob("g", "blob", "desc");
        GaugeArrayStatistic<1213, 2> array_stat("g", "array", "desc", 0);

        const SystemStatistic &gauge_base = gauge;
        const SystemStatistic &text_base = text;
        const SystemStatistic &blob_base = blob;
        const SystemStatistic &array_base = array_stat;

        POINTERS_EQUAL(&gauge, gauge_base.As<ScalarStatisticView>());
        POINTERS_EQUAL(nullptr, gauge_base.As<TextStatisticView>());
        POINTERS_EQUAL(nullptr, gauge_base.As<BlobStatisticView>());
        POINTERS_EQUAL(nullptr, gauge_base.As<ArrayStatisticView>());

        POINTERS_EQUAL(nullptr, text_base.As<ScalarStatisticView>());
        POINTERS_EQUAL(&text, text_base.As<TextStatisticView>());
        POINTERS_EQUAL(nullptr, text_base.As<BlobStatisticView>());
        POINTERS_EQUAL(nullptr, text_base.As<ArrayStatisticView>());

        POINTERS_EQUAL(nullptr, blob_base.As<ScalarStatisticView>());
        POINTERS_EQUAL(nullptr, blob_base.As<TextStatisticView>());
        POINTERS_EQUAL(&blob, blob_base.As<BlobStatisticView>());
        POINTERS_EQUAL(nullptr, blob_base.As<ArrayStatisticView>());

        POINTERS_EQUAL(nullptr, array_base.As<ScalarStatisticView>());
        POINTERS_EQUAL(nullptr, array_base.As<TextStatisticView>());
        POINTERS_EQUAL(nullptr, array_base.As<BlobStatisticView>());
        POINTERS_EQUAL(&array_stat, array_base.As<ArrayStatisticView>());
    }

    TEST(StatisticViewTests, RepresentationFollowsTheKind)
    {
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(RepresentationOf(StatKind::GAUGE)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(RepresentationOf(StatKind::COUNTER)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(RepresentationOf(StatKind::TIMESTAMP)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(RepresentationOf(StatKind::PROBE)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::TEXT), static_cast<int>(RepresentationOf(StatKind::TEXT)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::BLOB), static_cast<int>(RepresentationOf(StatKind::BLOB)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::ARRAY), static_cast<int>(RepresentationOf(StatKind::ARRAY)));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::UNKNOWN), static_cast<int>(RepresentationOf(static_cast<StatKind>(0))));
    }

    TEST(StatisticViewTests, EveryRegisteredStatisticReportsTheRepresentationOfItsKind)
    {
        uint32_t visited = 0;

        GetSystemStatisticsDB().ForEach([&visited](const SystemStatistic &stat)
        {
            CHECK_EQUAL(static_cast<int>(RepresentationOf(stat.Kind())), static_cast<int>(stat.Representation()));
            visited++;

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK(visited > 0);
    }

    TEST(StatisticViewTests, RepresentationsHaveNames)
    {
        STRCMP_EQUAL("scalar", ToString(ValueRepresentation::SCALAR));
        STRCMP_EQUAL("text", ToString(ValueRepresentation::TEXT));
        STRCMP_EQUAL("blob", ToString(ValueRepresentation::BLOB));
        STRCMP_EQUAL("array", ToString(ValueRepresentation::ARRAY));
        STRCMP_EQUAL("unknown", ToString(ValueRepresentation::UNKNOWN));
    }

    TEST(StatisticViewTests, TextIsReadThroughItsView)
    {
        TextStatistic<1220, 16> text("g", "text", "desc");
        const TextStatisticView *view = static_cast<const SystemStatistic &>(text).As<TextStatisticView>();
        char buffer[32];

        CHECK(view != nullptr);
        CHECK_EQUAL(16u, view->MaxLength());
        CHECK_FALSE(view->HasValue());

        CHECK_RESULT(SUCCESS, text.Set("hello"));

        CHECK_TRUE(view->HasValue());
        CHECK_EQUAL(5u, view->Length());
        CHECK_RESULT(SUCCESS, view->Get(buffer, sizeof(buffer)));
        STRCMP_EQUAL("hello", buffer);
    }

    TEST(StatisticViewTests, BlobIsReadThroughItsView)
    {
        BlobStatistic<1221, 8> blob("g", "blob", "desc");
        const BlobStatisticView *view = static_cast<const SystemStatistic &>(blob).As<BlobStatisticView>();
        uint8_t buffer[8] = {};

        CHECK(view != nullptr);
        CHECK_EQUAL(8u, view->MaxSize());
        CHECK_FALSE(view->HasValue());

        CHECK_RESULT(SUCCESS, blob.Set("abc", 3));

        CHECK_TRUE(view->HasValue());
        CHECK_EQUAL(3u, view->Size());
        CHECK_RESULT(SUCCESS, view->Get(buffer, sizeof(buffer)));
        CHECK_EQUAL('a', buffer[0]);
        CHECK_EQUAL('c', buffer[2]);
    }

    TEST(StatisticViewTests, SchemaEntriesRecordTheRepresentation)
    {
        SystemStatisticType gauge(SystemStatistic::KERNEL_PROCESS_ID, 1, StatKind::GAUGE, "g", "n", "d");
        SystemStatisticType text(SystemStatistic::KERNEL_PROCESS_ID, 2, StatKind::TEXT, "g", "n", "d");
        SystemStatisticType array_type(SystemStatistic::KERNEL_PROCESS_ID, 3, StatKind::ARRAY, "g", "n", "d");

        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(gauge.Representation()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::TEXT), static_cast<int>(text.Representation()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::ARRAY), static_cast<int>(array_type.Representation()));
    }

    TEST(StatisticViewTests, StaticLinksRecordTheirOwnStatistic)
    {
        for (const StaticStatisticLink *link = StaticStatisticLink::FirstStaticStatistic(); link != nullptr; link = link->NextStaticStatistic())
        {
            CHECK_EQUAL(SystemStatistic::KERNEL_PROCESS_ID, link->Statistic().ProcessId());
            CHECK_EQUAL(static_cast<int>(RepresentationOf(link->Statistic().Kind())), static_cast<int>(link->Statistic().Representation()));
        }

        CHECK_TRUE(IsRecorded(gauge_a_stat));
    }
}
