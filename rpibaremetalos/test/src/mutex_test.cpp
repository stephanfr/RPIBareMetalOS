// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "fake_wait_policy.h"

#include "task/basic_mutex.h"

namespace
{
    using fake_wait::FakeLock;
    using fake_wait::FakeTask;
    using fake_wait::FakeWaitPolicy;

    using Mutex = task::BasicMutex<FakeWaitPolicy>;

    FakeTask a('A'), b('B'), c('C');

    Mutex *mutex = nullptr;

    void ActAs(FakeTask &task)
    {
        FakeWaitPolicy::current_ = &task;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MutexHandoffTests)
    {
        Mutex the_mutex;

        void setup() override
        {
            FakeWaitPolicy::Reset();

            FakeTask *const tasks[] = {&a, &b, &c};

            for (FakeTask *task : tasks)
            {
                task->wait_queue_next_ = nullptr;
                task->wait_queue_prev_ = nullptr;
                task->status_ = task::WaitStatus::NONE;
                task->blocks_ = 0;
                task->on_block_ = nullptr;
            }

            mutex = &the_mutex;
        }
    };
#pragma GCC diagnostic pop

    TEST(MutexHandoffTests, UncontendedLockExcludesOtherTasksUntilUnlocked)
    {
        ActAs(a);
        mutex->Lock();

        ActAs(b);
        CHECK_FALSE(mutex->TryLock());

        ActAs(a);
        mutex->Unlock();

        ActAs(b);
        CHECK_TRUE(mutex->TryLock());
        mutex->Unlock();

        CHECK_EQUAL(0U, a.blocks_ + b.blocks_);
        CHECK_EQUAL(0U, FakeWaitPolicy::handoffs_);
    }

    TEST(MutexHandoffTests, ContendedLockBlocksUntilTheOwnerUnlocks)
    {
        ActAs(a);
        mutex->Lock();

        b.on_block_ = [](FakeTask &)
        {
            ActAs(a);
            mutex->Unlock();
        };

        ActAs(b);
        mutex->Lock();

        CHECK_EQUAL(1U, b.blocks_);
        STRCMP_EQUAL("B", FakeWaitPolicy::wake_log_);

        ActAs(a);
        CHECK_FALSE(mutex->TryLock());

        ActAs(b);
        mutex->Unlock();
        CHECK_EQUAL(0U, FakeWaitPolicy::handoffs_);
    }

    TEST(MutexHandoffTests, AWaiterThatLosesToABargerIsHandedTheLock)
    {
        static bool barger_refused = false;
        barger_refused = false;

        ActAs(a);
        mutex->Lock();

        //  First block: A releases, waking B, and takes the lock straight back before B runs.

        b.on_block_ = [](FakeTask &blocked)
        {
            ActAs(a);
            mutex->Unlock();
            mutex->Lock();

            //  Second block: B has set HANDOFF.  A third task cannot take the lock, and A's unlock hands it to B.

            blocked.on_block_ = [](FakeTask &)
            {
                ActAs(c);
                barger_refused = !mutex->TryLock();

                ActAs(a);
                mutex->Unlock();
            };
        };

        ActAs(b);
        mutex->Lock();

        CHECK_TRUE(barger_refused);
        CHECK_EQUAL(2U, b.blocks_);
        CHECK_EQUAL(1U, FakeWaitPolicy::handoffs_);

        ActAs(a);
        CHECK_FALSE(mutex->TryLock());

        ActAs(b);
        mutex->Unlock();

        ActAs(c);
        CHECK_TRUE(mutex->TryLock());
        mutex->Unlock();
    }

    TEST(MutexHandoffTests, HandoffWithNoWaiterLeftReleasesTheLock)
    {
        ActAs(a);
        mutex->Lock();

        //  B is woken, loses to A's barge and sets HANDOFF.  A unlocks before B is back on the queue: with nobody
        //      to hand to, the lock is released, and B takes it without blocking again.  The unlock has to land on
        //      B's second lock of the queue: its first is the one that unlinks it after the wake.

        b.on_block_ = [](FakeTask &)
        {
            ActAs(a);
            mutex->Unlock();
            mutex->Lock();

            FakeLock::before_lock_skip_ = 1;
            FakeLock::before_lock_ = []()
            {
                ActAs(a);
                mutex->Unlock();
                ActAs(b);
            };
        };

        ActAs(b);
        mutex->Lock();

        CHECK_EQUAL(1U, b.blocks_);
        CHECK_EQUAL(0U, FakeWaitPolicy::handoffs_);

        ActAs(c);
        CHECK_FALSE(mutex->TryLock());

        ActAs(b);
        mutex->Unlock();

        ActAs(c);
        CHECK_TRUE(mutex->TryLock());
        mutex->Unlock();
    }
}
