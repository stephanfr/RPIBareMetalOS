// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace system_stats
{
    //  The id of every kernel statistic (process SystemStatistic::KERNEL_PROCESS_ID), in one list so two statistics
    //      cannot be given the same id by accident: the database would only reject the second at boot.  Ids are
    //      never reused, so a removed statistic leaves a gap.

    typedef enum class KernelStatisticId : uint32_t
    {
        USER_TASK_FAULTS                = 1,

        MEMORY_FREE_PAGES               = 2,
        MEMORY_INVALID_RELEASES         = 3,
        MEMORY_ALLOC_FAILURES           = 4,
        MEMORY_LAST_INVALID_RELEASE     = 5,

        TASK_VOLUNTARY_SWITCHES         = 6,
        TASK_PREEMPTIVE_SWITCHES        = 7,
        TASK_TIMESLICES_GRANTED         = 8,
        TASK_RUNNING                    = 9,
        TASK_ZOMBIES                    = 10,
        TASK_DROPPED_ZOMBIES            = 11,
        TASK_RUNLIST_DEPTH_CORE0        = 12,
        TASK_RUNLIST_DEPTH_CORE1        = 13,
        TASK_RUNLIST_DEPTH_CORE2        = 14,
        TASK_RUNLIST_DEPTH_CORE3        = 15,

        SCHEDULER_MISSED_INTERRUPTS     = 16,

        MMU_ADDRESS_SPACES              = 17,
        MMU_DEMAND_PAGE_FAULTS          = 18,
        MMU_DEMAND_PAGE_FAILURES        = 19,
    } KernelStatisticId;

    //  For the statistic templates, which take the id as a uint32_t.

    constexpr uint32_t KernelStatistic(KernelStatisticId id)
    {
        return static_cast<uint32_t>(id);
    }
} // namespace system_stats
