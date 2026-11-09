// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  A single-threaded stand-in for the scheduler, for testing BasicWaitQueue and BasicMutex.  Concurrency is
//      simulated by hooks: when a task blocks, its on_block hook runs "while it is blocked", typically switching
//      FakeWaitPolicy::current to another task and acting as it.  When the hook returns, the blocked task resumes
//      with whatever status the hook left it: WOKEN if something woke it, TIMED_OUT if it had a deadline and
//      nothing did.  A task blocked with no deadline that nothing wakes is a deadlock, and fails the test.

#include "cpputest_support.h"

#include "task/basic_wait_queue.h"

namespace fake_wait
{
    struct FakeTask;

    using BlockHook = void (*)(FakeTask &blocked);
    using LockHook = void (*)();

    struct FakeTask
    {
        explicit FakeTask(char name)
            : name_(name)
        {
        }

        char name_;

        FakeTask *wait_queue_next_ = nullptr;
        FakeTask *wait_queue_prev_ = nullptr;

        task::WaitStatus status_ = task::WaitStatus::NONE;
        task::WaitReason reason_ = task::WaitReason::NONE;
        uint64_t deadline_ = 0;
        uint32_t blocks_ = 0;

        BlockHook on_block_ = nullptr; //  one-shot
    };

    class FakeLock
    {
    public:
        void Lock()
        {
            if (held_)
            {
                FAIL("wait queue lock taken twice");
            }

            if ((before_lock_ != nullptr) && (before_lock_skip_-- == 0))
            {
                LockHook hook = before_lock_;
                before_lock_ = nullptr;
                hook();
            }

            held_ = true;
        }

        void Unlock()
        {
            held_ = false;
        }

        //  Runs once, before a FakeLock is held, after before_lock_skip_ acquisitions have gone by without it.

        static inline LockHook before_lock_ = nullptr;
        static inline uint32_t before_lock_skip_ = 0;

    private:
        bool held_ = false;
    };

    struct FakeWaitPolicy
    {
        using Task = FakeTask;
        using Lock = FakeLock;

        struct IRQGuard
        {
        };

        static inline FakeTask *current_ = nullptr;
        static inline uint64_t now_ticks_ = 1000;
        static inline uint32_t preemption_points_ = 0;
        static inline uint32_t handoffs_ = 0;

        //  When set, Fatal() checks its message against this and ends the test as a pass, without returning.
        static inline const char *expected_fatal_ = nullptr;

        //  The order tasks were woken in, by name.
        static inline char wake_log_[32] = {};
        static inline uint32_t wakes_logged_ = 0;

        static void Reset()
        {
            current_ = nullptr;
            now_ticks_ = 1000;
            preemption_points_ = 0;
            handoffs_ = 0;
            expected_fatal_ = nullptr;
            wakes_logged_ = 0;
            wake_log_[0] = '\0';
            FakeLock::before_lock_ = nullptr;
            FakeLock::before_lock_skip_ = 0;
        }

        static FakeTask &Current()
        {
            return *current_;
        }

        static void PrepareToBlock(FakeTask &task, task::WaitReason reason, uint64_t deadline_ticks)
        {
            task.status_ = task::WaitStatus::WAITING;
            task.reason_ = reason;
            task.deadline_ = deadline_ticks;
        }

        static task::WaitStatus CommitBlock(FakeTask &task)
        {
            task.blocks_++;

            if (BlockHook hook = task.on_block_)
            {
                task.on_block_ = nullptr;
                hook(task);
            }

            current_ = &task;

            if (task.status_ == task::WaitStatus::WAITING)
            {
                if (task.deadline_ == 0)
                {
                    FAIL("a task blocked with no deadline and nothing woke it");
                }

                task.status_ = task::WaitStatus::TIMED_OUT;
            }

            const task::WaitStatus how = task.status_;
            task.status_ = task::WaitStatus::NONE;

            return how;
        }

        static bool Wake(FakeTask &task, task::WaitStatus how)
        {
            if (task.status_ != task::WaitStatus::WAITING)
            {
                return false;
            }

            task.status_ = how;

            if (wakes_logged_ < sizeof(wake_log_) - 1)
            {
                wake_log_[wakes_logged_++] = task.name_;
                wake_log_[wakes_logged_] = '\0';
            }

            return true;
        }

        static uint64_t NowTicks()
        {
            return now_ticks_;
        }

        //  One tick per microsecond.

        static uint64_t DeadlineAfter(microseconds timeout)
        {
            return now_ticks_ + static_cast<uint64_t>(timeout.count());
        }

        static void PreemptionPoint()
        {
            preemption_points_++;
        }

        static bool InInterrupt()
        {
            return false;
        }

        static void RecordHandoff()
        {
            handoffs_++;
        }

        [[noreturn]] static void Fatal(const char *message)
        {
            if (expected_fatal_ != nullptr)
            {
                STRCMP_EQUAL(expected_fatal_, message);
                TEST_EXIT;
            }

            FAIL(message);
            __builtin_unreachable();
        }
    };

    using WaitQueue = task::BasicWaitQueue<FakeWaitPolicy>;
} // namespace fake_wait
