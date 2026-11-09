// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "fake_wait_policy.h"

#include <string.h>

namespace
{
    using fake_wait::FakeTask;
    using fake_wait::FakeWaitPolicy;
    using fake_wait::WaitQueue;

    using Outcome = WaitQueue::Outcome;
    using Position = WaitQueue::Position;

    constexpr uint32_t MAX_CHAIN = 4;

    //  Blocks tasks_[0], tasks_[1], ... on queue_ one after another, each from inside the previous one's block, so
    //      all are blocked at once when finally_ runs.  Their WaitOnce() outcomes are recorded as they resume.

    struct Chain
    {
        static inline WaitQueue *queue_ = nullptr;
        static inline FakeTask *tasks_[MAX_CHAIN] = {};
        static inline Position positions_[MAX_CHAIN] = {};
        static inline Outcome outcomes_[MAX_CHAIN] = {};
        static inline uint32_t count_ = 0;
        static inline uint32_t next_ = 0;
        static inline uint64_t deadline_ = 0;
        static inline bool condition_ = false;
        static inline void (*finally_)() = nullptr;

        static void Start(WaitQueue &queue, uint32_t count, void (*finally)(), uint64_t deadline = 0)
        {
            queue_ = &queue;
            count_ = count;
            next_ = 0;
            deadline_ = deadline;
            finally_ = finally;

            BlockNext(*tasks_[0]);
        }

        static void BlockNext(FakeTask &)
        {
            if (next_ == count_)
            {
                finally_();
                return;
            }

            const uint32_t i = next_++;
            FakeTask &task = *tasks_[i];

            task.on_block_ = BlockNext;
            FakeWaitPolicy::current_ = &task;

            outcomes_[i] = queue_->WaitOnce([]() { return condition_; }, task::WaitReason::EVENT, deadline_, positions_[i]);
        }
    };

    FakeTask a('A'), b('B'), c('C'), waker('W');

    void ResetAll()
    {
        FakeWaitPolicy::Reset();

        FakeTask *const tasks[] = {&a, &b, &c, &waker};

        for (FakeTask *task : tasks)
        {
            task->wait_next_ = nullptr;
            task->wait_prev_ = nullptr;
            task->status_ = task::WaitStatus::NONE;
            task->blocks_ = 0;
            task->on_block_ = nullptr;
        }

        Chain::tasks_[0] = &a;
        Chain::tasks_[1] = &b;
        Chain::tasks_[2] = &c;

        for (uint32_t i = 0; i < MAX_CHAIN; i++)
        {
            Chain::positions_[i] = Position::TAIL;
            Chain::outcomes_[i] = Outcome::CONDITION_MET;
        }

        Chain::condition_ = false;
    }

    int AsInt(Outcome outcome)
    {
        return static_cast<int>(outcome);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(WaitQueueConditionTests)
    {
        void setup() override
        {
            ResetAll();
        }
    };
#pragma GCC diagnostic pop

    TEST(WaitQueueConditionTests, ConditionAlreadyMetReturnsWithoutBlocking)
    {
        WaitQueue queue;
        FakeWaitPolicy::current_ = &a;

        CHECK_EQUAL(AsInt(Outcome::CONDITION_MET), AsInt(queue.WaitOnce([]() { return true; }, task::WaitReason::EVENT, 0, Position::TAIL)));
        queue.Wait([]() { return true; }, task::WaitReason::EVENT);
        CHECK_TRUE(queue.Wait([]() { return true; }, task::WaitReason::EVENT, microseconds(10)));

        CHECK_EQUAL(0U, a.blocks_);
    }

    TEST(WaitQueueConditionTests, ExpiredDeadlineReturnsWithoutBlocking)
    {
        WaitQueue queue;
        FakeWaitPolicy::current_ = &a;

        CHECK_EQUAL(AsInt(Outcome::TIMED_OUT),
                    AsInt(queue.WaitOnce([]() { return false; }, task::WaitReason::EVENT, FakeWaitPolicy::now_ticks_, Position::TAIL)));
        CHECK_FALSE(queue.Wait([]() { return false; }, task::WaitReason::EVENT, microseconds(0)));

        CHECK_EQUAL(0U, a.blocks_);
    }

    TEST(WaitQueueConditionTests, TimeoutReportsAConditionThatBecameTrue)
    {
        WaitQueue queue;

        Chain::Start(queue, 1, []() { Chain::condition_ = true; }, FakeWaitPolicy::now_ticks_ + 10);

        CHECK_EQUAL(AsInt(Outcome::CONDITION_MET), AsInt(Chain::outcomes_[0]));
        CHECK_EQUAL(1U, a.blocks_);
    }

    TEST(WaitQueueConditionTests, WaitBlocksAgainAfterAWakeThatLeavesTheConditionFalse)
    {
        static WaitQueue queue;
        static bool ready = false;
        ready = false;

        a.on_block_ = [](FakeTask &blocked)
        {
            FakeWaitPolicy::current_ = &waker;
            CHECK_EQUAL(1U, queue.WakeAll());

            blocked.on_block_ = [](FakeTask &)
            {
                FakeWaitPolicy::current_ = &waker;
                ready = true;
                CHECK_EQUAL(1U, queue.WakeOne());
            };
        };

        FakeWaitPolicy::current_ = &a;
        queue.Wait([]() { return ready; }, task::WaitReason::EVENT);

        CHECK_EQUAL(2U, a.blocks_);
        STRCMP_EQUAL("AA", FakeWaitPolicy::wake_log_);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(WaitQueueLinkTests)
    {
        void setup() override
        {
            ResetAll();
        }
    };
#pragma GCC diagnostic pop

    TEST(WaitQueueLinkTests, WakeOneWakesWaitersInArrivalOrder)
    {
        WaitQueue queue;

        Chain::Start(queue, 3, []()
        {
            FakeWaitPolicy::current_ = &waker;

            CHECK_EQUAL(1U, Chain::queue_->WakeOne());
            CHECK_EQUAL(1U, Chain::queue_->WakeOne());
            CHECK_EQUAL(1U, Chain::queue_->WakeOne());
            CHECK_EQUAL(0U, Chain::queue_->WakeOne());
        });

        STRCMP_EQUAL("ABC", FakeWaitPolicy::wake_log_);

        for (uint32_t i = 0; i < 3; i++)
        {
            CHECK_EQUAL(AsInt(Outcome::WOKEN), AsInt(Chain::outcomes_[i]));
        }
    }

    TEST(WaitQueueLinkTests, HeadPositionJumpsTheQueue)
    {
        WaitQueue queue;

        Chain::positions_[2] = Position::HEAD;

        Chain::Start(queue, 3, []()
        {
            FakeWaitPolicy::current_ = &waker;
            CHECK_EQUAL(3U, Chain::queue_->WakeAll());
        });

        STRCMP_EQUAL("CAB", FakeWaitPolicy::wake_log_);
    }

    TEST(WaitQueueLinkTests, WakeOneSkipsAWaiterWhoseWaitAlreadyEnded)
    {
        WaitQueue queue;

        Chain::Start(queue, 2, []()
        {
            a.status_ = task::WaitStatus::TIMED_OUT; //  as if its timer fired first

            FakeWaitPolicy::current_ = &waker;
            CHECK_EQUAL(1U, Chain::queue_->WakeOne());
        }, FakeWaitPolicy::now_ticks_ + 10);

        STRCMP_EQUAL("B", FakeWaitPolicy::wake_log_);
        CHECK_EQUAL(AsInt(Outcome::TIMED_OUT), AsInt(Chain::outcomes_[0]));
        CHECK_EQUAL(AsInt(Outcome::WOKEN), AsInt(Chain::outcomes_[1]));
        CHECK_EQUAL(0U, queue.WakeOne());
    }

    TEST(WaitQueueLinkTests, TimedOutWaitersUnlinkThemselves)
    {
        WaitQueue queue;

        Chain::Start(queue, 3, []() {}, FakeWaitPolicy::now_ticks_ + 10);

        for (uint32_t i = 0; i < 3; i++)
        {
            CHECK_EQUAL(AsInt(Outcome::TIMED_OUT), AsInt(Chain::outcomes_[i]));
            POINTERS_EQUAL(nullptr, Chain::tasks_[i]->wait_next_);
            POINTERS_EQUAL(nullptr, Chain::tasks_[i]->wait_prev_);
        }

        CHECK_EQUAL(0U, queue.WakeAll());
        STRCMP_EQUAL("", FakeWaitPolicy::wake_log_);
    }

    TEST(WaitQueueLinkTests, WakeAllCountsOnlyTheWaitsItEnded)
    {
        WaitQueue queue;

        Chain::Start(queue, 3, []()
        {
            b.status_ = task::WaitStatus::TIMED_OUT;

            FakeWaitPolicy::current_ = &waker;
            CHECK_EQUAL(2U, Chain::queue_->WakeAll());
            CHECK_EQUAL(0U, Chain::queue_->WakeAll());
        }, FakeWaitPolicy::now_ticks_ + 10);

        STRCMP_EQUAL("AC", FakeWaitPolicy::wake_log_);
    }

    TEST(WaitQueueLinkTests, EveryWakeEndsAtAPreemptionPoint)
    {
        WaitQueue queue;
        FakeWaitPolicy::current_ = &waker;

        queue.WakeOne();
        queue.WakeAll();
        queue.WakeOne([](FakeTask *) {});

        CHECK_EQUAL(3U, FakeWaitPolicy::preemption_points_);
    }

    TEST(WaitQueueLinkTests, OnPopSeesEachWakeTriedThenNullWhenNoneSucceeds)
    {
        WaitQueue queue;

        Chain::Start(queue, 2, []()
        {
            a.status_ = task::WaitStatus::TIMED_OUT;
            b.status_ = task::WaitStatus::TIMED_OUT;

            static char popped[4] = {};
            static uint32_t count = 0;
            count = 0;

            FakeWaitPolicy::current_ = &waker;

            CHECK_EQUAL(0U, Chain::queue_->WakeOne([](FakeTask *task) { popped[count++] = (task != nullptr) ? task->name_ : '-'; }));

            popped[count] = '\0';
            STRCMP_EQUAL("AB-", popped);
        }, FakeWaitPolicy::now_ticks_ + 10);
    }
}
