// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/wait_queue.h"

#include "task/mutex.h"
#include "task/task_impl.h"
#include "task/task_manager_impl.h"

#include "devices/log.h"
#include "devices/physical_timer.h"

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"

#include "utility/tick_conversion.h"

namespace task
{
    namespace
    {
        system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::MUTEX_HANDOFFS>
            mutex_handoffs("mutex", "handoffs", "Mutexes passed straight to a waiter that had lost to a barger", 0);
    }

    TaskImpl &KernelWaitPolicy::Current()
    {
        return TaskImpl::GetTask();
    }

    void KernelWaitPolicy::PrepareToBlock(TaskImpl &task, WaitReason reason, uint64_t deadline_ticks)
    {
        task.PrepareToBlock(reason, deadline_ticks);
    }

    WaitStatus KernelWaitPolicy::CommitBlock(TaskImpl &task)
    {
        return task.CommitBlock();
    }

    bool KernelWaitPolicy::Wake(TaskImpl &task, WaitStatus how)
    {
        return TaskManagerImpl::Instance().WakeTask(task, how);
    }

    uint64_t KernelWaitPolicy::NowTicks()
    {
        return PhysicalTimer::CurrentTicks();
    }

    uint64_t KernelWaitPolicy::DeadlineAfter(microseconds timeout)
    {
        const int64_t timeout_us = (timeout.count() > 0) ? timeout.count() : 0;

        return PhysicalTimer::CurrentTicks() + utility::NanosecondsToTicks(static_cast<uint64_t>(timeout_us) * 1000, GetCounterFrequency());
    }

    void KernelWaitPolicy::PreemptionPoint()
    {
        TaskImpl::GetTask().PreemptionPoint();
    }

    bool KernelWaitPolicy::InInterrupt()
    {
        return ::InInterrupt();
    }

    void KernelWaitPolicy::RecordHandoff()
    {
        mutex_handoffs.Increment();
    }

    void KernelWaitPolicy::Fatal(const char *message)
    {
        LogFatal("%s: task %s\n", message, TaskImpl::GetTask().Name().c_str());
        ParkCore();

        while (true)
        {
        }
    }

    void Mutex::Lock()
    {
        mutex_.Lock();
    }

    void Mutex::Unlock()
    {
        mutex_.Unlock();
    }

    bool Mutex::TryLock()
    {
        return mutex_.TryLock();
    }
} // namespace task
