// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "services/stats_db/system_stats.h"

namespace system_stats
{
    //  Ids of the compile-time kernel statistics, in one list so none is used twice by accident.  Ids are never
    //      reused.  Dynamic kernel statistics share this id space.

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
        TASK_RUNLIST_DEPTH              = 12,

        SCHEDULER_MISSED_INTERRUPTS     = 16,

        MMU_ADDRESS_SPACES              = 17,
        MMU_DEMAND_PAGE_FAULTS          = 18,
        MMU_DEMAND_PAGE_FAILURES        = 19,

        UPTIME_TICKS                    = 20,

        EMMC_READS                      = 21,
        EMMC_WRITES                     = 22,
        EMMC_BLOCKS_READ                = 23,
        EMMC_BLOCKS_WRITTEN             = 24,
        EMMC_IO_FAILURES                = 25,
        EMMC_LAST_IO_FAILURE            = 26,
        EMMC_COMMAND_RETRIES            = 27,
        EMMC_IO_TIME_US                 = 28,
        EMMC_MAX_IO_TIME_US             = 29,
        EMMC_CONTROLLER_WAITS           = 30,
        EMMC_CARD_RESETS                = 31,
        EMMC_TRANSFER_ABORTS            = 32,

        HEAP_DYNAMIC_ALLOCATIONS        = 33,

        TASK_WAITING                    = 34,
        TASK_WAITING_IO                 = 35,
        TASK_BLOCKS                     = 36,
        TASK_WAKEUPS                    = 37,
        TASK_WAIT_TIMEOUTS              = 38,

        MUTEX_HANDOFFS                  = 39,

        SCHEDULER_DEADLINE_INTERRUPTS   = 40,

        CONSOLE_RX_INTERRUPTS           = 41,
        CONSOLE_RX_BYTES                = 42,
        CONSOLE_RX_DROPPED              = 43,
        CONSOLE_RX_ERRORS               = 44,
        CONSOLE_WAKE_LATENCY_MAX_US     = 45,

        RP1_MSIX_DELIVERIES             = 46,
        RP1_IACKS                       = 47,

        DRIVER_STARTS                   = 48,
        DRIVER_RESTARTS                 = 49,

        TASK_KILLS                      = 50,

        CONSOLE_TX_BYPASSED             = 51,
    } KernelStatisticId;

    constexpr uint32_t KernelStatistic(KernelStatisticId id)
    {
        return static_cast<uint32_t>(id);
    }

    //  The compile-time statistics keyed by KernelStatisticId:
    //
    //      KernelCounterStatistic<KernelStatisticId::TASK_VOLUNTARY_SWITCHES> voluntary_switches("tasks", ...);

    template<KernelStatisticId Id>
    using KernelGaugeStatistic = GaugeStatistic<KernelStatistic(Id)>;

    template<KernelStatisticId Id>
    using KernelCounterStatistic = CounterStatistic<KernelStatistic(Id)>;

    template<KernelStatisticId Id>
    using KernelTimestampStatistic = TimestampStatistic<KernelStatistic(Id)>;

    template<KernelStatisticId Id>
    using KernelProbeStatistic = ProbeStatistic<KernelStatistic(Id)>;

    template<KernelStatisticId Id, uint32_t MaxLen, uint32_t NumSlots = 4>
    using KernelTextStatistic = TextStatistic<KernelStatistic(Id), MaxLen, NumSlots>;

    template<KernelStatisticId Id, uint32_t MaxBytes, uint32_t NumSlots = 4>
    using KernelBlobStatistic = BlobStatistic<KernelStatistic(Id), MaxBytes, NumSlots>;

    template<KernelStatisticId Id, uint32_t Capacity>
    using KernelGaugeArrayStatistic = GaugeArrayStatistic<KernelStatistic(Id), Capacity>;
} // namespace system_stats
