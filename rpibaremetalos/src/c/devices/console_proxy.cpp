// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/console_proxy.h"

#include <string.h>

#include <lockfree/spsc_queue>

#include "asm_utility.h"
#include "devices/character_io.h"
#include "devices/physical_timer.h"
#include "heaps.h"
#include "os_entity.h"
#include "platform/hypervisor.h"
#include "services/driver_manager.h"
#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"
#include "synchronization.h"
#include "task/channels.h"
#include "task/io_doorbell.h"
#include "task/task_impl.h"
#include "uapi/console.h"
#include "utility/tick_conversion.h"

namespace
{
    using minstd::chrono::duration_cast;
    using minstd::chrono::microseconds;
    using minstd::chrono::milliseconds;

    using system_stats::KernelStatisticId;

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_INTERRUPTS>
        rx_interrupts("console", "rx_interrupts", "Console UART receive interrupts", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_BYTES>
        rx_bytes("console", "rx_bytes", "Bytes the console driver passed to the kernel", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_DROPPED>
        rx_dropped("console", "rx_dropped", "Received bytes lost because the input ring was full", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_RX_ERRORS>
        rx_errors("console", "rx_errors", "Received bytes with a framing, parity, break or overrun error", 0);

    system_stats::KernelGaugeStatistic<KernelStatisticId::CONSOLE_WAKE_LATENCY_MAX_US>
        wake_latency_max_us("console", "wake_latency_max_us", "Longest time from console input to the blocked reader running, in microseconds", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::CONSOLE_TX_BYPASSED>
        tx_bypassed("console", "tx_bypassed", "Output bytes sent through EL2 because the driver's ring stayed full", 0);

    constexpr uint32_t RX_RING_SIZE = 256;

    //  A writer that may sleep waits this long for a driver that isn't taking bytes, then restarts it.  One that may
    //      not sleep spins only briefly.  Either then sends the byte through EL2.

    constexpr milliseconds STALL_LIMIT(2000);
    constexpr milliseconds SPIN_LIMIT(10);

    constexpr milliseconds INPUT_POLL(10);
    constexpr milliseconds FLUSH_LIMIT(200);

    constexpr uint32_t RING_MASK = CONSOLE_TX_RING_SIZE - 1;

    static_assert((CONSOLE_TX_RING_SIZE & RING_MASK) == 0, "the ring's size is a power of two");

    task::Channel &ConsoleChannel()
    {
        return GetChannel(ChannelId::CONSOLE);
    }

    class ConsoleProxy : public CharacterIODevice, public task::ChannelListener
    {
    public:
        explicit ConsoleProxy(CharacterIODevice &uart)
            : CharacterIODevice(true, "console", "CONSOLE"),
              uart_(uart)
        {
        }

        //  The driver's own output, a byte from an ISR that interrupted it included, goes through EL2: it can't
        //      wait for itself.

        void putc(unsigned int c) override
        {
            const char byte = static_cast<char>(c);
            const uint32_t server = ConsoleChannel().Server();

            if ((server == 0) || (task::TaskImpl::GetTask().ProcessId() == server))
            {
                Hypervisor::ConsoleWrite(&byte, 1);
                return;
            }

            if (!TryPush(byte))
            {
                WaitToPush(byte);
            }
        }

        unsigned int getc() override
        {
            for (;;)
            {
                uint8_t byte;

                if (rx_ring_.pop_front(byte))
                {
                    return Translated(byte);
                }

                if (ConsoleChannel().Server() != 0)
                {
                    rx_doorbell_.Wait(task::WaitReason::DEVICE);

                    if (rx_doorbell_.LastWakeLatencyTicks() != 0)
                    {
                        RecordWakeLatency(rx_doorbell_.LastWakeLatencyTicks());
                    }

                    continue;
                }

                unsigned int polled;

                if (uart_.TryGetc(polled))
                {
                    return Translated(polled);
                }

                task::Task::GetTask().SleepFor(INPUT_POLL);
            }
        }

        bool EnableInterruptDrivenInput() override
        {
            return uart_.EnableInterruptSource();
        }

        void Posted(const uint8_t *message, size_t length) override
        {
            ConsoleInputHeader header;

            if (length < sizeof(header))
            {
                return;
            }

            memcpy(&header, message, sizeof(header));

            rx_interrupts.Increment(header.interrupts);
            rx_errors.Increment(header.errors);

            for (size_t i = sizeof(header); i < length; i++)
            {
                if (rx_ring_.push_back(message[i]))
                {
                    rx_bytes.Increment();
                }
                else
                {
                    rx_dropped.Increment();
                }
            }

            rx_doorbell_.Ring();
        }

        //  The channel is unserved, so new output already goes through EL2.  A writer that saw it served a moment ago
        //      may still add a byte after this, which the next driver sends.

        void Released() override
        {
            char chunk[64];

            for (;;)
            {
                size_t count = 0;

                {
                    Guard guard(lock_);

                    uint32_t tail = SharedTail();

                    if ((head_ - tail) > CONSOLE_TX_RING_SIZE)
                    {
                        tail = head_ - CONSOLE_TX_RING_SIZE;
                    }

                    while ((tail != head_) && (count < sizeof(chunk)))
                    {
                        chunk[count++] = static_cast<char>(Ring()[tail & RING_MASK]);
                        tail++;
                    }

                    if (count == 0)
                    {
                        head_ = 0;
                        PublishHead();
                        __atomic_store_n(Index(CONSOLE_TX_TAIL_OFFSET), 0, __ATOMIC_RELEASE);
                        break;
                    }

                    __atomic_store_n(Index(CONSOLE_TX_TAIL_OFFSET), tail, __ATOMIC_RELEASE);
                }

                Hypervisor::ConsoleWrite(chunk, count);
            }

            rx_doorbell_.Ring();
        }

        void Flush()
        {
            const auto deadline = PhysicalTimer::Now() + FLUSH_LIMIT;

            while ((ConsoleChannel().Server() != 0) && (PhysicalTimer::Now() < deadline))
            {
                uint32_t head;

                {
                    Guard guard(lock_);
                    head = head_;
                }

                if (SharedTail() == head)
                {
                    return;
                }
            }
        }

    private:
        //  The producers' lock, with IRQs off: putc may come from an ISR.

        class Guard
        {
        public:
            explicit Guard(SpinLock &lock)
                : irqs_(SaveAndDisableIRQs()),
                  lock_(lock)
            {
                lock_.Lock();
            }

            ~Guard()
            {
                lock_.Unlock();
                RestoreIRQs(irqs_);
            }

            Guard(const Guard &) = delete;
            Guard &operator=(const Guard &) = delete;

        private:
            uint64_t irqs_;
            SpinLock &lock_;
        };

        CharacterIODevice &uart_;

        SpinLock lock_;
        uint32_t head_ = 0;                     //  under lock_; the driver can rewrite the shared copy, so not that

        static_allocator<uint8_t> rx_allocator_;
        minstd::spsc_queue<uint8_t> rx_ring_{rx_allocator_, RX_RING_SIZE};
        task::IODoorbell rx_doorbell_;

        static unsigned int Translated(unsigned int c)
        {
            return (c == '\r') ? '\n' : c;
        }

        static uint8_t *DataArea()
        {
            return ConsoleChannel().Memory() + CHANNEL_DATA_OFFSET;
        }

        static uint32_t *Index(size_t offset)
        {
            return reinterpret_cast<uint32_t *>(DataArea() + offset);
        }

        static uint8_t *Ring()
        {
            return DataArea() + CONSOLE_TX_RING_OFFSET;
        }

        static uint32_t SharedTail()
        {
            return __atomic_load_n(Index(CONSOLE_TX_TAIL_OFFSET), __ATOMIC_ACQUIRE);
        }

        void PublishHead()
        {
            __atomic_store_n(Index(CONSOLE_TX_HEAD_OFFSET), head_, __ATOMIC_RELEASE);
        }

        //  False when the ring is full, or the driver has moved the tail somewhere impossible.

        bool TryPush(char byte)
        {
            {
                Guard guard(lock_);

                if ((head_ - SharedTail()) >= CONSOLE_TX_RING_SIZE)
                {
                    return false;
                }

                Ring()[head_ & RING_MASK] = static_cast<uint8_t>(byte);
                head_++;
                PublishHead();
            }

            ConsoleChannel().Signal();

            return true;
        }

        //  The clock restarts whenever the driver takes bytes, so a driver that is only slow is waited for.

        void WaitToPush(char byte)
        {
            task::TaskImpl &self = task::TaskImpl::GetTask();
            const bool may_block = self.MayBlock();
            const milliseconds limit = may_block ? STALL_LIMIT : SPIN_LIMIT;

            auto progress_at = PhysicalTimer::Now();
            uint32_t tail = SharedTail();

            while (ConsoleChannel().Server() != 0)
            {
                if (may_block)
                {
                    self.SleepFor(milliseconds(1));
                }

                if (TryPush(byte))
                {
                    return;
                }

                const auto now = PhysicalTimer::Now();

                if (SharedTail() != tail)
                {
                    tail = SharedTail();
                    progress_at = now;
                }
                else if (duration_cast<milliseconds>(now - progress_at) >= limit)
                {
                    if (may_block)
                    {
                        RestartDriver(DriverId::CONSOLE);
                    }

                    break;
                }
            }

            tx_bypassed.Increment();
            Hypervisor::ConsoleWrite(&byte, 1);
        }

        //  Only the reader task updates the maximum, so Get() then Set() does not race.

        static void RecordWakeLatency(uint64_t ticks)
        {
            const int64_t us = static_cast<int64_t>(utility::TicksToNanoseconds(ticks, GetCounterFrequency()) / 1000);

            if (us > wake_latency_max_us.Get())
            {
                wake_latency_max_us.Set(us);
            }
        }
    };

    ConsoleProxy *console = nullptr;
}

bool RegisterConsole()
{
    auto serial = GetOSEntityRegistry().GetEntityByAlias<CharacterIODevice>("SERIAL");

    if (serial.Failed())
    {
        return false;
    }

    auto proxy = make_static_unique<ConsoleProxy>(*serial);

    console = proxy.get();
    SetChannelListener(ChannelId::CONSOLE, console);

    GetOSEntityRegistry().AddEntity(proxy);

    return true;
}

void FlushConsole()
{
    if (console != nullptr)
    {
        console->Flush();
    }
}
