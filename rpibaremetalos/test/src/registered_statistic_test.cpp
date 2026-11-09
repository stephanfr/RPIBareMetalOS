// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The ready-made dynamic statistics, RegisteredStatistic -- which registers one and frees it only once no reader can
//      reach it -- and the KernelStatisticId aliases for the compile-time statistics.

#include "cpputest_support.h"
#include "stats_db_test_support.h"

#include <stdint.h>
#include <string.h>

#include <utility>

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/registered_statistic.h"
#include "services/stats_db/stats_db.h"

namespace
{
    using namespace system_stats;

#define CHECK_DB_RESULT(expected, expression) CHECK_EQUAL(static_cast<int>(SystemStatisticsDB::StatsDBResultCodes::expected), static_cast<int>(expression))
#define CHECK_VALUE_RESULT(expected, expression) CHECK_EQUAL(static_cast<int>(StatisticValueResultCodes::expected), static_cast<int>(expression))

    //  Forwards to stats_db_test_resource and counts what it has handed out and not had back: the statistics'
    //      own memory, apart from the database's map nodes.

    class CountingResource : public minstd::pmr::memory_resource
    {
    public:
        uint32_t LiveAllocations() const
        {
            return live_allocations_;
        }

    private:
        uint32_t live_allocations_ = 0;

        void *do_allocate(size_t bytes, size_t alignment) override
        {
            void *block = stats_db_test_resource.allocate(bytes, alignment);

            if (block != nullptr)
            {
                live_allocations_++;
            }

            return block;
        }

        void do_deallocate(void *block, size_t bytes, size_t alignment) override
        {
            live_allocations_--;
            stats_db_test_resource.deallocate(block, bytes, alignment);
        }

        bool do_is_equal(minstd::pmr::memory_resource const &other) const noexcept override
        {
            return this == &other;
        }
    };

    int64_t ValueOf(const SystemStatisticsDB &db, uint32_t id)
    {
        int64_t value = -1;

        db.Visit(SystemStatistic::KERNEL_PROCESS_ID, id, [&value](const SystemStatistic &stat)
        {
            const auto result = stat.GetValue<int64_t>();

            CHECK_TRUE(result.Successful());
            value = *result;
        });

        return value;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(RegisteredStatisticTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;
        CountingResource resource;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        }

        void teardown() override
        {
            CHECK_EQUAL(0u, resource.LiveAllocations());
        }
    };
#pragma GCC diagnostic pop

    TEST(RegisteredStatisticTests, RegisterAddsTheStatisticToTheDatabase)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;

        CHECK_FALSE(gauge.Registered());
        CHECK_DB_RESULT(SUCCESS, gauge.Register(db, resource, 5000, "net", "queue_depth", "Packets waiting", 7));
        CHECK_TRUE(gauge.Registered());

        POINTERS_EQUAL(gauge.Statistic(), FoundStatistic(db, 5000));
        CHECK_EQUAL(1u, db.Size());
        CHECK_EQUAL((int64_t)7, ValueOf(db, 5000));
        CHECK_EQUAL(1u, resource.LiveAllocations());
    }

    TEST(RegisteredStatisticTests, DestructorRemovesAndFreesTheStatistic)
    {
        {
            RegisteredStatistic<DynamicCounterStatistic> counter;

            CHECK_DB_RESULT(SUCCESS, counter.Register(db, resource, 5001, "net", "packets", "Packets received"));
            CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5001));
        }

        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5001));
        CHECK_EQUAL(0u, db.Size());
        CHECK_EQUAL(0u, resource.LiveAllocations());
    }

    TEST(RegisteredStatisticTests, RetireRemovesAndFreesTheStatistic)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;

        CHECK_DB_RESULT(SUCCESS, gauge.Register(db, resource, 5002, "net", "queue_depth", "Packets waiting"));
        CHECK_DB_RESULT(SUCCESS, gauge.Retire());

        CHECK_FALSE(gauge.Registered());
        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5002));
        CHECK_EQUAL(0u, resource.LiveAllocations());

        CHECK_DB_RESULT(SUCCESS, gauge.Retire());          //  holding nothing, there is nothing to do
    }

    TEST(RegisteredStatisticTests, AStatisticRetiredDuringAReadIsFreedOnlyOnceTheReadEnds)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;

        CHECK_DB_RESULT(SUCCESS, gauge.Register(db, resource, 5003, "net", "queue_depth", "Packets waiting", 11));

        int64_t value_seen_after_retire = 0;

        db.ForEach([&](const SystemStatistic &stat)
        {
            CHECK_DB_RESULT(SUCCESS, gauge.Retire());

            //  The reader still holds stat: it is out of the database but not freed.

            CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5003));
            CHECK_EQUAL(1u, resource.LiveAllocations());
            CHECK_EQUAL(0u, ReclaimRetiredStatistics());

            value_seen_after_retire = *stat.GetValue<int64_t>();

            return SystemStatisticsDB::VisitAction::CONTINUE;
        });

        CHECK_EQUAL((int64_t)11, value_seen_after_retire);
        CHECK_EQUAL(1u, resource.LiveAllocations());

        CHECK_EQUAL(1u, ReclaimRetiredStatistics());        //  what the reaper does
        CHECK_EQUAL(0u, resource.LiveAllocations());
    }

    TEST(RegisteredStatisticTests, AFailedRegistrationKeepsNothing)
    {
        RegisteredStatistic<DynamicGaugeStatistic> first;
        RegisteredStatistic<DynamicCounterStatistic> duplicate;

        CHECK_DB_RESULT(SUCCESS, first.Register(db, resource, 5004, "net", "first", "desc"));
        CHECK_DB_RESULT(DUPLICATE_ID, duplicate.Register(db, resource, 5004, "net", "duplicate", "desc"));

        CHECK_FALSE(duplicate.Registered());
        CHECK_EQUAL(1u, resource.LiveAllocations());        //  only first's
        POINTERS_EQUAL(first.Statistic(), FoundStatistic(db, 5004));

        SystemStatisticsDB uninitialized;
        RegisteredStatistic<DynamicGaugeStatistic> no_database;

        CHECK_DB_RESULT(NOT_INITIALIZED, no_database.Register(uninitialized, resource, 5005, "net", "none", "desc"));
        CHECK_FALSE(no_database.Registered());
        CHECK_EQUAL(1u, resource.LiveAllocations());
    }

    TEST(RegisteredStatisticTests, AnAllocationFailureIsReported)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;

        stats_db_test_resource.FailAllocations(true);
        const auto result = gauge.Register(db, resource, 5006, "net", "queue_depth", "desc");
        stats_db_test_resource.FailAllocations(false);

        CHECK_DB_RESULT(UNABLE_TO_ALLOCATE_MEMORY, result);
        CHECK_FALSE(gauge.Registered());
        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5006));
    }

    TEST(RegisteredStatisticTests, ASecondRegisterIsRefusedAndKeepsTheFirst)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;

        CHECK_DB_RESULT(SUCCESS, gauge.Register(db, resource, 5007, "net", "first", "desc", 1));
        CHECK_DB_RESULT(ALREADY_REGISTERED, gauge.Register(db, resource, 5008, "net", "second", "desc", 2));

        CHECK_EQUAL(5007u, gauge.Statistic()->id());
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5007));
        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5008));
        CHECK_EQUAL(1u, resource.LiveAllocations());
    }

    TEST(RegisteredStatisticTests, MovingTransfersOwnership)
    {
        RegisteredStatistic<DynamicGaugeStatistic> original;

        CHECK_DB_RESULT(SUCCESS, original.Register(db, resource, 5009, "net", "moved", "desc"));

        const DynamicGaugeStatistic *statistic = original.Statistic();

        RegisteredStatistic<DynamicGaugeStatistic> moved(minstd::move(original));

        CHECK_FALSE(original.Registered());
        CHECK_TRUE(moved.Registered());
        POINTERS_EQUAL(statistic, moved.Statistic());

        //  Assigning over a registered statistic retires it first.

        RegisteredStatistic<DynamicGaugeStatistic> other;

        CHECK_DB_RESULT(SUCCESS, other.Register(db, resource, 5010, "net", "other", "desc"));

        moved = minstd::move(other);

        CHECK_FALSE(other.Registered());
        CHECK_FALSE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5009));
        CHECK_TRUE(db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 5010));
        CHECK_EQUAL(5010u, moved.Statistic()->id());
        CHECK_EQUAL(1u, resource.LiveAllocations());
    }

    TEST(RegisteredStatisticTests, OperationsWithoutAStatisticReportNotRegistered)
    {
        RegisteredStatistic<DynamicGaugeStatistic> never_registered;
        RegisteredStatistic<DynamicGaugeStatistic> retired;
        RegisteredStatistic<DynamicGaugeStatistic> moved_from;
        RegisteredStatistic<DynamicTimestampStatistic> timestamp;

        CHECK_DB_RESULT(SUCCESS, retired.Register(db, resource, 5011, "net", "retired", "desc"));
        CHECK_DB_RESULT(SUCCESS, retired.Retire());

        CHECK_DB_RESULT(SUCCESS, moved_from.Register(db, resource, 5012, "net", "moved", "desc"));
        RegisteredStatistic<DynamicGaugeStatistic> moved_to(minstd::move(moved_from));

        for (RegisteredStatistic<DynamicGaugeStatistic> *gauge : {&never_registered, &retired, &moved_from})
        {
            CHECK_FALSE(gauge->Registered());
            POINTERS_EQUAL(nullptr, gauge->Statistic());
            CHECK_VALUE_RESULT(NOT_REGISTERED, gauge->Get().ResultCode());
            CHECK_VALUE_RESULT(NOT_REGISTERED, gauge->Set(1));
            CHECK_VALUE_RESULT(NOT_REGISTERED, gauge->Increment().ResultCode());
        }

        CHECK_VALUE_RESULT(NOT_REGISTERED, timestamp.Stamp());
        CHECK_VALUE_RESULT(NOT_REGISTERED, timestamp.Stamp(5));
        CHECK_VALUE_RESULT(NOT_REGISTERED, timestamp.HasOccurred().ResultCode());

        //  The one moved to is unaffected.

        CHECK_VALUE_RESULT(SUCCESS, moved_to.Set(3));
        CHECK_EQUAL((int64_t)3, ValueOf(db, 5012));
    }

    TEST(RegisteredStatisticTests, RegisterInKernelUsesTheKernelDatabase)
    {
        SystemStatisticsDB &kernel_db = GetSystemStatisticsDB();
        const uint32_t size_before = kernel_db.Size();

        {
            RegisteredStatistic<DynamicCounterStatistic> counter;

            CHECK_DB_RESULT(SUCCESS, counter.RegisterInKernel(9000, "net", "kernel_db", "desc", 41));
            CHECK_EQUAL((int64_t)41, *counter.Increment());

            POINTERS_EQUAL(counter.Statistic(), FoundStatistic(kernel_db, 9000));
            CHECK_EQUAL((int64_t)42, ValueOf(kernel_db, 9000));
            CHECK_EQUAL(size_before + 1, kernel_db.Size());
        }

        CHECK_FALSE(kernel_db.Contains(SystemStatistic::KERNEL_PROCESS_ID, 9000));
        CHECK_EQUAL(size_before, kernel_db.Size());
    }

    TEST(RegisteredStatisticTests, ManyStatisticsComeAndGo)
    {
        for (uint32_t round = 0; round < 3; round++)
        {
            RegisteredStatistic<DynamicCounterStatistic> counters[16];

            for (uint32_t i = 0; i < 16; i++)
            {
                CHECK_DB_RESULT(SUCCESS, counters[i].Register(db, resource, 6000 + i, "net", "counter", "desc", i));
            }

            CHECK_EQUAL(16u, db.Size());
            CHECK_EQUAL((int64_t)9, ValueOf(db, 6009));
        }

        CHECK_EQUAL(0u, db.Size());
        CHECK_EQUAL(SystemStatisticsDB::DEFAULT_CAPACITY, db.Available());
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(DynamicKernelStatisticTests, StatsDBTestFixture)
    {
        SystemStatisticsDB db;
        CountingResource resource;

        void setup() override
        {
            StatsDBTestFixture::setup();
            CHECK_DB_RESULT(SUCCESS, db.Initialize(stats_db_test_resource));
        }

        void teardown() override
        {
            CHECK_EQUAL(0u, resource.LiveAllocations());
        }
    };
#pragma GCC diagnostic pop

    TEST(DynamicKernelStatisticTests, MetadataIsTheKernelsAndTheKindIsFixedByTheClass)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;
        RegisteredStatistic<DynamicCounterStatistic> counter;
        RegisteredStatistic<DynamicTimestampStatistic> timestamp;

        CHECK_DB_RESULT(SUCCESS, gauge.Register(db, resource, 7000, "g", "gauge", "A gauge"));
        CHECK_DB_RESULT(SUCCESS, counter.Register(db, resource, 7001, "g", "counter", "A counter"));
        CHECK_DB_RESULT(SUCCESS, timestamp.Register(db, resource, 7002, "g", "timestamp", "A timestamp"));

        CHECK_EQUAL(7000u, gauge.Statistic()->id());
        CHECK_EQUAL(SystemStatistic::KERNEL_PROCESS_ID, gauge.Statistic()->ProcessId());
        STRCMP_EQUAL("g", gauge.Statistic()->Group());
        STRCMP_EQUAL("gauge", gauge.Statistic()->Name());
        STRCMP_EQUAL("A gauge", gauge.Statistic()->Description());

        CHECK_EQUAL(static_cast<int>(StatKind::GAUGE), static_cast<int>(gauge.Statistic()->Kind()));
        CHECK_EQUAL(static_cast<int>(StatKind::COUNTER), static_cast<int>(counter.Statistic()->Kind()));
        CHECK_EQUAL(static_cast<int>(StatKind::TIMESTAMP), static_cast<int>(timestamp.Statistic()->Kind()));

        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(gauge.Statistic()->Representation()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(counter.Statistic()->Representation()));
        CHECK_EQUAL(static_cast<int>(ValueRepresentation::SCALAR), static_cast<int>(timestamp.Statistic()->Representation()));
    }

    TEST(DynamicKernelStatisticTests, StringsAreCopiedTruncatedAndNeverNull)
    {
        char name[] = "built_at_run_time";

        RegisteredStatistic<DynamicGaugeStatistic> copied;
        RegisteredStatistic<DynamicGaugeStatistic> long_strings;
        RegisteredStatistic<DynamicGaugeStatistic> null_strings;

        CHECK_DB_RESULT(SUCCESS, copied.Register(db, resource, 7010, "g", name, "desc"));

        name[0] = 'X';                                      //  the statistic kept its own copy
        STRCMP_EQUAL("built_at_run_time", copied.Statistic()->Name());

        const char *too_long = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";

        CHECK_DB_RESULT(SUCCESS, long_strings.Register(db, resource, 7011, too_long, too_long, too_long));
        CHECK_EQUAL(SystemStatistic::GROUP_MAX, strnlen(long_strings.Statistic()->Group(), 200));
        CHECK_EQUAL(SystemStatistic::NAME_MAX, strnlen(long_strings.Statistic()->Name(), 200));
        CHECK_EQUAL(SystemStatistic::DESCRIPTION_MAX, strnlen(long_strings.Statistic()->Description(), 200));

        CHECK_DB_RESULT(SUCCESS, null_strings.Register(db, resource, 7012, nullptr, nullptr, nullptr));
        STRCMP_EQUAL("", null_strings.Statistic()->Group());
        STRCMP_EQUAL("", null_strings.Statistic()->Name());
        STRCMP_EQUAL("", null_strings.Statistic()->Description());
    }

    TEST(DynamicKernelStatisticTests, OperationsMatchTheCompileTimeStatistics)
    {
        RegisteredStatistic<DynamicGaugeStatistic> gauge;
        RegisteredStatistic<DynamicCounterStatistic> counter;
        RegisteredStatistic<DynamicTimestampStatistic> timestamp;

        CHECK_DB_RESULT(SUCCESS, gauge.Register(db, resource, 7020, "g", "gauge", "desc", 10));
        CHECK_DB_RESULT(SUCCESS, counter.Register(db, resource, 7021, "g", "counter", "desc", 100));
        CHECK_DB_RESULT(SUCCESS, timestamp.Register(db, resource, 7022, "g", "timestamp", "desc"));

        CHECK_VALUE_RESULT(SUCCESS, gauge.Set(-3));
        CHECK_EQUAL((int64_t)-3, *gauge.Increment(5));      //  the previous value
        CHECK_EQUAL((int64_t)2, *gauge.Get());

        CHECK_EQUAL((int64_t)100, *counter.Increment());
        CHECK_EQUAL((int64_t)101, *counter.Increment(4));
        CHECK_EQUAL((int64_t)105, *counter.Get());

        CHECK_FALSE(*timestamp.HasOccurred());
        CHECK_VALUE_RESULT(SUCCESS, timestamp.Stamp(123456));
        CHECK_TRUE(*timestamp.HasOccurred());
        CHECK_EQUAL((int64_t)123456, *timestamp.Get());

        //  Readers see the same values through the database.

        CHECK_EQUAL((int64_t)2, ValueOf(db, 7020));
        CHECK_EQUAL((int64_t)105, ValueOf(db, 7021));
        CHECK_EQUAL((int64_t)123456, ValueOf(db, 7022));
    }

    TEST(DynamicKernelStatisticTests, ATimestampStampsTheCurrentTicks)
    {
        RegisteredStatistic<DynamicTimestampStatistic> timestamp;

        CHECK_DB_RESULT(SUCCESS, timestamp.Register(db, resource, 7030, "g", "timestamp", "desc"));

        TimestampTickSource::SetTickFunction([]() -> int64_t { return 4242; });
        CHECK_VALUE_RESULT(SUCCESS, timestamp.Stamp());
        TimestampTickSource::SetTickFunction(nullptr);

        CHECK_EQUAL((int64_t)4242, *timestamp.Get());
    }

    TEST(DynamicKernelStatisticTests, ATimestampExposesItsTicksAsATimePoint)
    {
        RegisteredStatistic<DynamicTimestampStatistic> timestamp;

        CHECK_DB_RESULT(SUCCESS, timestamp.Register(db, resource, 7031, "g", "timestamp", "desc"));

        TimestampTickSource::SetTickFrequency(1000000);   //  1 MHz: one tick is one microsecond
        CHECK_VALUE_RESULT(SUCCESS, timestamp.Stamp(3));
        CHECK_EQUAL(3000, minstd::chrono::duration_cast<minstd::chrono::nanoseconds>(timestamp.AsTimePoint().time_since_epoch()).count());
        TimestampTickSource::SetTickFrequency(0);         //  restore host default
    }

    //  Compile-time checks: each concept is true when the expression in it compiles.

    template<typename Statistic>
    concept ConstructibleByAnyone = requires { Statistic(1u, "g", "n", "d"); };

    template<typename Statistic>
    concept CanOwn = requires { typename RegisteredStatistic<Statistic>; };

    template<auto Id>
    concept NamesAKernelGauge = requires { typename KernelGaugeStatistic<Id>; };

    template<typename Owner>
    concept CanSet = requires(Owner &owner) { owner.Set(1); };

    template<typename Owner>
    concept CanIncrement = requires(Owner &owner) { owner.Increment(); };

    template<typename Owner>
    concept CanStamp = requires(Owner &owner) { owner.Stamp(); };

    template<typename Owner>
    concept CanDereference = requires(Owner &owner) { owner.operator->(); } || requires(Owner &owner) { *owner; };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP_BASE(DynamicStatisticCompileTimeTests, StatsDBTestFixture)
    {};
#pragma GCC diagnostic pop

    TEST(DynamicStatisticCompileTimeTests, OnlyRegisteredStatisticCreatesTheReadyMadeStatistics)
    {
        static_assert(!ConstructibleByAnyone<DynamicGaugeStatistic>);
        static_assert(!ConstructibleByAnyone<DynamicCounterStatistic>);
        static_assert(!ConstructibleByAnyone<DynamicTimestampStatistic>);

        static_assert(DynamicStatisticType<DynamicGaugeStatistic>);
        static_assert(DynamicStatisticType<DynamicCounterStatistic>);
        static_assert(DynamicStatisticType<DynamicTimestampStatistic>);
    }

    TEST(DynamicStatisticCompileTimeTests, OnlyDynamicStatisticsCanBeOwned)
    {
        static_assert(CanOwn<DynamicGaugeStatistic>);
        static_assert(!CanOwn<GaugeStatistic<1>>);              //  compile-time statistics are never removed
        static_assert(!CanOwn<TestScalarStatistic>);
        static_assert(!CanOwn<int>);
    }

    TEST(DynamicStatisticCompileTimeTests, AnOwnerHasOnlyItsStatisticsOperations)
    {
        static_assert(CanSet<RegisteredStatistic<DynamicGaugeStatistic>>);
        static_assert(CanIncrement<RegisteredStatistic<DynamicGaugeStatistic>>);
        static_assert(!CanStamp<RegisteredStatistic<DynamicGaugeStatistic>>);

        static_assert(!CanSet<RegisteredStatistic<DynamicCounterStatistic>>);    //  a counter only moves forward
        static_assert(CanIncrement<RegisteredStatistic<DynamicCounterStatistic>>);
        static_assert(!CanStamp<RegisteredStatistic<DynamicCounterStatistic>>);

        static_assert(!CanSet<RegisteredStatistic<DynamicTimestampStatistic>>);
        static_assert(!CanIncrement<RegisteredStatistic<DynamicTimestampStatistic>>);
        static_assert(CanStamp<RegisteredStatistic<DynamicTimestampStatistic>>);

        static_assert(!CanDereference<RegisteredStatistic<DynamicGaugeStatistic>>);   //  nothing to dereference by mistake
    }

    TEST(DynamicStatisticCompileTimeTests, KernelAliasesTakeOnlyAKernelStatisticId)
    {
        static_assert(minstd::same_as<KernelCounterStatistic<KernelStatisticId::TASK_VOLUNTARY_SWITCHES>,
                                      CounterStatistic<KernelStatistic(KernelStatisticId::TASK_VOLUNTARY_SWITCHES)>>);
        static_assert(minstd::same_as<KernelGaugeArrayStatistic<KernelStatisticId::TASK_RUNLIST_DEPTH, 4>,
                                      GaugeArrayStatistic<KernelStatistic(KernelStatisticId::TASK_RUNLIST_DEPTH), 4>>);

        static_assert(NamesAKernelGauge<KernelStatisticId::TASK_RUNNING>);
        static_assert(!NamesAKernelGauge<9u>);                  //  a bare number is not a KernelStatisticId
    }
}
