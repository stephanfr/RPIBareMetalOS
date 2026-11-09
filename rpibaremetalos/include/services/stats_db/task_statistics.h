// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"

namespace system_stats
{
    //  The task/scheduler statistics updated from more than one file, declared here so every site
    //      can reach them:
    //
    //      task_running / task_zombies   move in TaskImpl::Exit(), TaskExecutionContext::SwitchTasks()
    //                                    and TaskManagerImpl::ReapZombies()
    //      task_dropped_zombies          moves in TaskManagerImpl::NotifyTaskDelisted()
    //      scheduler_missed_interrupts   moves in both scheduler clocks (scheduler_clock.cpp, system_timer.cpp)
    //
    //      Defined in the owning module (task_manager_impl.cpp, scheduler_clock.cpp); like every other
    //      kernel statistic they are recorded during static initialisation and registered at boot, so the
    //      update sites need no registration call of their own.

    extern KernelGaugeStatistic<KernelStatisticId::TASK_RUNNING> task_running;
    extern KernelGaugeStatistic<KernelStatisticId::TASK_ZOMBIES> task_zombies;
    extern KernelCounterStatistic<KernelStatisticId::TASK_DROPPED_ZOMBIES> task_dropped_zombies;
    extern KernelCounterStatistic<KernelStatisticId::SCHEDULER_MISSED_INTERRUPTS> scheduler_missed_interrupts;
} // namespace system_stats
