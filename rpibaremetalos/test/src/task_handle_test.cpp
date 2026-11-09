// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  TaskHandle -- the counted reference FindTask() returns.  Every handle must hold exactly one reference while it is
//      non-empty, or the reaper frees a task still in use (too few) or never frees it (too many).

#include "cpputest_support.h"

#include "task/tasks.h"

namespace
{
    class CountingTask : public task::Task
    {
    public:
        uint32_t references_ = 0;
        uint32_t joins_ = 0;

        const UUID &ID() const override { return id_; }
        const minstd::string &Name() const override { return name_; }
        TaskType Type() const override { return TaskType::KERNEL_TASK; }
        ExecutionState State() const override { return ExecutionState::RUNNING; }

        uint32_t CurrentCore() const override { return 0; }
        microseconds Runtime() const override { return microseconds::zero(); }
        uint64_t TimeslicesGranted() const override { return 0; }

        task::WaitReason CurrentWaitReason() const override { return task::WaitReason::NONE; }
        microseconds IOWaitTime() const override { return microseconds::zero(); }

        void Yield() override {}
        void Exit() override {}
        void Join() override { joins_++; }

    protected:
        void AddReference() override { references_++; }
        void ReleaseReference() override { references_--; }

    private:
        UUID id_;
        minstd::fixed_string<16> name_{"counting"};
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(TaskHandleTests)
    {};
#pragma GCC diagnostic pop

    TEST(TaskHandleTests, DefaultHandleIsEmpty)
    {
        task::TaskHandle handle;

        CHECK_FALSE(handle.has_value());
        CHECK_FALSE(static_cast<bool>(handle));
    }

    TEST(TaskHandleTests, HandleHoldsOneReferenceForItsLifetime)
    {
        CountingTask task;

        {
            task::TaskHandle handle(task);

            CHECK_TRUE(handle.has_value());
            CHECK_TRUE(static_cast<bool>(handle));
            CHECK_EQUAL(1, task.references_);
        }

        CHECK_EQUAL(0, task.references_);
    }

    TEST(TaskHandleTests, AccessorsReachTheTask)
    {
        CountingTask task;
        task::TaskHandle handle(task);

        POINTERS_EQUAL(&task, handle.operator->());
        POINTERS_EQUAL(&task, &(*handle));

        handle->Join();
        (*handle).Join();
        CHECK_EQUAL(2, task.joins_);
    }

    TEST(TaskHandleTests, CopiesEachHoldTheirOwnReference)
    {
        CountingTask task;

        {
            task::TaskHandle original(task);
            task::TaskHandle copied(original);
            CHECK_EQUAL(2, task.references_);

            task::TaskHandle assigned;
            assigned = original;
            CHECK_EQUAL(3, task.references_);
            POINTERS_EQUAL(&task, assigned.operator->());
        }

        CHECK_EQUAL(0, task.references_);
    }

    TEST(TaskHandleTests, MovesTransferTheReferenceAndEmptyTheSource)
    {
        CountingTask task;

        {
            task::TaskHandle original(task);

            task::TaskHandle moved(minstd::move(original));
            CHECK_FALSE(original.has_value());
            CHECK_EQUAL(1, task.references_);

            task::TaskHandle assigned;
            assigned = minstd::move(moved);
            CHECK_FALSE(moved.has_value());
            CHECK_TRUE(assigned.has_value());
            CHECK_EQUAL(1, task.references_);
        }

        CHECK_EQUAL(0, task.references_);
    }

    TEST(TaskHandleTests, AssignmentReleasesThePreviousTask)
    {
        CountingTask first;
        CountingTask second;

        task::TaskHandle handle(first);

        handle = task::TaskHandle(second);
        CHECK_EQUAL(0, first.references_);
        CHECK_EQUAL(1, second.references_);

        handle = task::TaskHandle();
        CHECK_FALSE(handle.has_value());
        CHECK_EQUAL(0, second.references_);
    }

    TEST(TaskHandleTests, SelfAssignmentKeepsExactlyOneReference)
    {
        CountingTask task;
        task::TaskHandle handle(task);

        task::TaskHandle &alias = handle;
        handle = alias;
        CHECK_EQUAL(1, task.references_);

        handle = minstd::move(alias);
        CHECK_TRUE(handle.has_value());
        CHECK_EQUAL(1, task.references_);
    }
}
