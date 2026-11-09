// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "isr/deadline_timer_isr.h"

#include "devices/deadline_timer.h"

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"

#include "task/task_manager_impl.h"

namespace
{
    system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::SCHEDULER_DEADLINE_INTERRUPTS>
        deadline_interrupts("scheduler", "deadline_interrupts", "Interrupts from the per-core wait deadline timers", 0);
}

void DeadlineTimerISR::HandleInterrupt()
{
    DeadlineTimer::Disarm();

    deadline_interrupts.Increment();

    task::TaskManagerImpl::SetReschedPending(GetCoreID());
}
