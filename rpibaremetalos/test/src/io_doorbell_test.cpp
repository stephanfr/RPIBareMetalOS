// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "fake_wait_policy.h"

#include "task/basic_io_doorbell.h"

namespace
{
    using fake_wait::FakeTask;
    using fake_wait::FakeWaitPolicy;

    using IODoorbell = task::BasicIODoorbell<FakeWaitPolicy>;

    FakeTask a('A'), b('B');

    IODoorbell *doorbell = nullptr;

    void ResetAll(IODoorbell &under_test)
    {
        FakeWaitPolicy::Reset();

        FakeTask *const tasks[] = {&a, &b};

        for (FakeTask *task : tasks)
        {
            task->status_ = task::WaitStatus::NONE;
            task->blocks_ = 0;
            task->on_block_ = nullptr;
            task->on_prepare_ = nullptr;
        }

        doorbell = &under_test;
        FakeWaitPolicy::current_ = &a;
    }

    //  Each Ring() moves the clock on, so a latency measured from the wrong ring shows up.

    void RingOnce(FakeTask &)
    {
        doorbell->Ring();
    }

    void RingThreeTimes(FakeTask &)
    {
        for (int i = 0; i < 3; i++)
        {
            doorbell->Ring();
            FakeWaitPolicy::now_ticks_ += 10;
        }
    }

    void TimeOutThenRing(FakeTask &blocked)
    {
        CHECK_TRUE(FakeWaitPolicy::Wake(blocked, task::WaitStatus::TIMED_OUT));
        doorbell->Ring();
    }

    void RingThenTimeOut(FakeTask &blocked)
    {
        doorbell->Ring();
        CHECK_FALSE(FakeWaitPolicy::Wake(blocked, task::WaitStatus::TIMED_OUT));
    }

    void RingThenAdvanceClock(FakeTask &)
    {
        doorbell->Ring();
        FakeWaitPolicy::now_ticks_ += 250;
    }

    void SecondTaskWaits(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;
        doorbell->Wait(task::WaitReason::DEVICE);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(IODoorbellRaceTests)
    {
        IODoorbell under_test;

        void setup() override
        {
            ResetAll(under_test);
        }
    };
#pragma GCC diagnostic pop

    TEST(IODoorbellRaceTests, RingBeforeWaitIsConsumedWithoutBlocking)
    {
        doorbell->Ring();

        CHECK_TRUE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        CHECK_EQUAL(0U, a.blocks_);

        //  Consumed: the next wait has nothing to return for.

        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        CHECK_EQUAL(1U, a.blocks_);
    }

    TEST(IODoorbellRaceTests, RingBetweenCheckAndPublishEndsWaitAtOnce)
    {
        a.on_prepare_ = RingOnce;

        CHECK_TRUE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        STRCMP_EQUAL("A", FakeWaitPolicy::wake_log_);

        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
    }

    TEST(IODoorbellRaceTests, RingWhileBlockedWakesTheWaiter)
    {
        a.on_block_ = RingOnce;

        doorbell->Wait(task::WaitReason::DEVICE);

        CHECK_EQUAL(1U, a.blocks_);
        STRCMP_EQUAL("A", FakeWaitPolicy::wake_log_);
        CHECK_EQUAL(static_cast<int>(task::WaitReason::DEVICE), static_cast<int>(a.reason_));

        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
    }

    TEST(IODoorbellRaceTests, RingThatLosesToATimeoutStillCounts)
    {
        a.on_block_ = TimeOutThenRing;
        CHECK_TRUE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));

        a.on_block_ = RingThenTimeOut;
        CHECK_TRUE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));

        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
    }

    TEST(IODoorbellRaceTests, RingsWhileBlockedCoalesceIntoOneWake)
    {
        a.on_block_ = RingThreeTimes;

        doorbell->Wait(task::WaitReason::DEVICE);

        STRCMP_EQUAL("A", FakeWaitPolicy::wake_log_);
        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
    }

    TEST(IODoorbellRaceTests, TimeoutLeavesTheDoorbellReusable)
    {
        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        CHECK_EQUAL(100U, a.deadline_ - FakeWaitPolicy::now_ticks_);

        doorbell->Ring();
        CHECK_TRUE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        CHECK_EQUAL(1U, a.blocks_);
    }

    TEST(IODoorbellRaceTests, SecondConcurrentWaiterIsFatal)
    {
        FakeWaitPolicy::expected_fatal_ = "IODoorbell has a second waiter";
        a.on_block_ = SecondTaskWaits;

        doorbell->Wait(task::WaitReason::DEVICE);

        FAIL("a second waiter was not detected");
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(IODoorbellLatencyTests)
    {
        IODoorbell under_test;

        void setup() override
        {
            ResetAll(under_test);
        }
    };
#pragma GCC diagnostic pop

    TEST(IODoorbellLatencyTests, LatencyRunsFromRingToResume)
    {
        a.on_block_ = RingThenAdvanceClock;
        doorbell->Wait(task::WaitReason::DEVICE);
        CHECK_EQUAL(250U, doorbell->LastWakeLatencyTicks());

        //  Measured from the last of several coalesced rings.

        a.on_block_ = RingThreeTimes;
        doorbell->Wait(task::WaitReason::DEVICE);
        CHECK_EQUAL(10U, doorbell->LastWakeLatencyTicks());
    }

    TEST(IODoorbellLatencyTests, NoLatencyWithoutABlockedRing)
    {
        a.on_block_ = RingThenAdvanceClock;
        doorbell->Wait(task::WaitReason::DEVICE);

        doorbell->Ring();
        CHECK_TRUE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        CHECK_EQUAL(0U, doorbell->LastWakeLatencyTicks());

        CHECK_FALSE(doorbell->Wait(task::WaitReason::DEVICE, microseconds(100)));
        CHECK_EQUAL(0U, doorbell->LastWakeLatencyTicks());
    }
} // namespace
