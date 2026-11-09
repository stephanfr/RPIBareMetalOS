// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/uart_base.h"

#include "asm_utility.h"
#include "utility/tick_conversion.h"

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"

namespace
{
    using system_stats::KernelStatisticId;

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_INTERRUPTS>
        rx_interrupts("console", "rx_interrupts", "Console UART receive interrupts", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_BYTES>
        rx_bytes("console", "rx_bytes", "Bytes moved from the console UART FIFO to the input ring", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_DROPPED>
        rx_dropped("console", "rx_dropped", "Received bytes lost because the input ring was full", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_ERRORS>
        rx_errors("console", "rx_errors", "Received bytes with a framing, parity, break or overrun error", 0);

    system_stats::KernelGaugeStatistic<KernelStatisticId::CONSOLE_WAKE_LATENCY_MAX_US>
        wake_latency_max_us("console", "wake_latency_max_us", "Longest time from a receive interrupt to the blocked reader running, in microseconds", 0);
}

void ConsoleRxStatistics::Interrupt()
{
    rx_interrupts.Increment();
}

void ConsoleRxStatistics::Byte()
{
    rx_bytes.Increment();
}

void ConsoleRxStatistics::Dropped()
{
    rx_dropped.Increment();
}

void ConsoleRxStatistics::Error()
{
    rx_errors.Increment();
}

//  Only the reader task updates the maximum, so Get() then Set() does not race.

void ConsoleRxStatistics::WakeLatency(uint64_t ticks)
{
    const int64_t microseconds = static_cast<int64_t>(utility::TicksToNanoseconds(ticks, GetCounterFrequency()) / 1000);

    if (microseconds > wake_latency_max_us.Get())
    {
        wake_latency_max_us.Set(microseconds);
    }
}
