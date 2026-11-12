// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/sd_card_proxy.h"

#include "devices/emmc.h"
#include "devices/gpio.h"
#include "devices/log.h"
#include "devices/physical_timer.h"
#include "heaps.h"
#include "platform/gpu_mailbox_messages.h"
#include "platform/platform_info.h"
#include "services/driver_manager.h"
#include "services/stats_db/kernel_statistic_ids.h"
#include "task/channels.h"
#include "task/mutex.h"
#include "task/task_impl.h"
#include "uapi/sd_card.h"

namespace
{
    using minstd::chrono::duration_cast;
    using minstd::chrono::microseconds;
    using minstd::chrono::milliseconds;
    using system_stats::KernelStatisticId;

    //  At boot the driver brings the card up, retrying for several seconds; after a crash the reaper releases the
    //      driver and the manager waits out its delay first.

    constexpr milliseconds DRIVER_WAIT(20000);
    constexpr milliseconds DRIVER_POLL(10);

    //  The driver's own waits give a sick card several seconds a command before it fails the request, so only a call
    //      far longer than that means the driver has hung.

    constexpr milliseconds CALL_TIMEOUT_BASE(10000);
    constexpr milliseconds CALL_TIMEOUT_PER_BLOCK(100);

    //  Read and write requests are counted once each, however many calls they took.  The time statistics measure a
    //      request from when it holds the card to when it completes; controller_waits counts the requests that waited
    //      for another task's.

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_READS>
        reads("emmc", "reads", "Read requests completed", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_WRITES>
        writes("emmc", "writes", "Write requests completed", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_BLOCKS_READ>
        blocks_read("emmc", "blocks_read", "Blocks read by completed requests", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_BLOCKS_WRITTEN>
        blocks_written("emmc", "blocks_written", "Blocks written by completed requests", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_IO_FAILURES>
        io_failures("emmc", "io_failures", "Read or write requests that failed", 0);

    system_stats::KernelTimestampStatistic<KernelStatisticId::EMMC_LAST_IO_FAILURE>
        last_io_failure("emmc", "last_io_failure", "Tick of the most recent failed read or write request");

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_COMMAND_RETRIES>
        command_retries("emmc", "command_retries", "Data commands the driver reissued after a controller error", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_IO_TIME_US>
        io_time_us("emmc", "io_time_us", "Microseconds spent servicing read and write requests", 0);

    system_stats::KernelGaugeStatistic<KernelStatisticId::EMMC_MAX_IO_TIME_US>
        max_io_time_us("emmc", "max_io_time_us", "Longest single read or write request, in microseconds", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_CONTROLLER_WAITS>
        controller_waits("emmc", "controller_waits", "Requests that blocked because another task held the card", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_CARD_RESETS>
        card_resets("emmc", "card_resets", "Card reset attempts the driver made bringing the card up", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::EMMC_TRANSFER_ABORTS>
        transfer_aborts("emmc", "transfer_aborts", "Data commands the driver abandoned, resetting the controller", 0);

    class SDCardProxy final : public ExternalMassMediaController
    {
    public:
        SDCardProxy()
            : ExternalMassMediaController(true, "SD CARD", "SD CARD")
        {
        }

        uint32_t BlockSize() const override
        {
            return SD_CARD_BLOCK_SIZE;
        }

        BlockIOResultCodes Initialize() override
        {
            return AwaitDriver() ? BlockIOResultCodes::SUCCESS : BlockIOResultCodes::SD_DRIVER_UNAVAILABLE;
        }

        BlockIOResultCodes Seek(uint64_t offset_in_blocks) override
        {
            offset_in_blocks_ = offset_in_blocks;

            return BlockIOResultCodes::SUCCESS;
        }

        ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read) override
        {
            return Request(false, buffer, block_number, blocks_to_read);
        }

        ValueResult<BlockIOResultCodes, uint32_t> ReadFromCurrentOffset(uint8_t *buffer, uint32_t blocks_to_read) override
        {
            return ReadFromBlock(buffer, offset_in_blocks_, blocks_to_read);
        }

        ValueResult<BlockIOResultCodes, uint32_t> WriteBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_write) override
        {
            return Request(true, buffer, block_number, blocks_to_write);
        }

    private:
        task::Mutex lock_;
        uint64_t offset_in_blocks_ = 0;

        static bool AwaitDriver()
        {
            const task::Channel &channel = GetChannel(ChannelId::SD_CARD);

            for (milliseconds waited(0); channel.Server() == 0; waited += DRIVER_POLL)
            {
                if (waited >= DRIVER_WAIT)
                {
                    return false;
                }

                task::TaskImpl::GetTask().SleepFor(duration_cast<microseconds>(DRIVER_POLL));
            }

            return true;
        }

        ValueResult<BlockIOResultCodes, uint32_t> Request(bool write, uint8_t *buffer, uint32_t block, uint32_t count)
        {
            using Result = ValueResult<BlockIOResultCodes, uint32_t>;

            if (!lock_.TryLock())
            {
                controller_waits.Increment();
                lock_.Lock();
            }

            const auto start = PhysicalTimer::Now();

            const BlockIOResultCodes result = Transfer(write, buffer, block, count);

            Record(write, count, result, duration_cast<microseconds>(PhysicalTimer::Now() - start).count());

            lock_.Unlock();

            if (result != BlockIOResultCodes::SUCCESS)
            {
                return Result::Failure(result);
            }

            return Result::Success(count);
        }

        //  Waits for a driver only before the first call: one that dies part way fails the request.

        static BlockIOResultCodes Transfer(bool write, uint8_t *buffer, uint32_t block, uint32_t count)
        {
            if ((count > 0) && !AwaitDriver())
            {
                return BlockIOResultCodes::SD_DRIVER_UNAVAILABLE;
            }

            while (count > 0)
            {
                const uint32_t blocks = (count < SD_CARD_MAX_BLOCKS) ? count : SD_CARD_MAX_BLOCKS;
                const size_t bytes = blocks * SD_CARD_BLOCK_SIZE;

                SDCardRequest request{write ? static_cast<uint32_t>(SD_CARD_WRITE) : static_cast<uint32_t>(SD_CARD_READ), block, blocks};
                SDCardReply reply{};

                task::ChannelCall call;

                call.request = &request;
                call.request_length = sizeof(request);
                call.reply = &reply;
                call.reply_capacity = sizeof(reply);

                if (write)
                {
                    call.data_out = buffer;
                    call.data_out_length = bytes;
                }
                else
                {
                    call.data_in = buffer;
                    call.data_in_capacity = bytes;
                }

                const int result = GetChannel(ChannelId::SD_CARD).Call(call, CALL_TIMEOUT_BASE + (CALL_TIMEOUT_PER_BLOCK * blocks));

                if (result == CHANNEL_TIMED_OUT)
                {
                    RestartDriver(DriverId::SD_CARD);
                }

                if ((result != CHANNEL_SUCCESS) || (call.reply_length != sizeof(reply)))
                {
                    return BlockIOResultCodes::SD_DRIVER_FAILED;
                }

                //  The counts are the driver's word, but a wrong one costs nothing worse than a wrong statistic.

                command_retries.Increment(reply.command_retries);
                transfer_aborts.Increment(reply.transfer_aborts);
                card_resets.Increment(reply.card_resets);

                if (reply.result == SD_CARD_IO_FAILED)
                {
                    return BlockIOResultCodes::EMMC_DATA_COMMAND_MAX_RETRIES;
                }

                if ((reply.result != SD_CARD_SUCCESS) || (reply.blocks != blocks) || (!write && (call.data_in_length != bytes)))
                {
                    return BlockIOResultCodes::SD_DRIVER_FAILED;
                }

                buffer += bytes;
                block += blocks;
                count -= blocks;
            }

            return BlockIOResultCodes::SUCCESS;
        }

        //  With the card held: that serializes the read-compare-set of the max gauge.

        static void Record(bool write, uint32_t blocks, BlockIOResultCodes result, int64_t elapsed_us)
        {
            io_time_us.Increment(elapsed_us);

            if (elapsed_us > max_io_time_us.Get())
            {
                max_io_time_us.Set(elapsed_us);
            }

            if (result != BlockIOResultCodes::SUCCESS)
            {
                io_failures.Increment();
                last_io_failure.Stamp();
                return;
            }

            if (write)
            {
                writes.Increment();
                blocks_written.Increment(blocks);
            }
            else
            {
                reads.Increment();
                blocks_read.Increment(blocks);
            }
        }
    };

    uint32_t sd_clock_hz = 0;

    uint32_t ControllerClockHz()
    {
        if (sd_clock_hz != 0)
        {
            return sd_clock_hz;
        }

        GPUMailbox mailbox(GetPlatformInfo().GetMailboxRegisterBase());
        GetClockRateTag clock_rate_tag(MailboxClockIdentifiers(1));
        GPUMailboxPropertyMessage message(clock_rate_tag);

        mailbox.sendMessage(message);

        const uint32_t rate = clock_rate_tag.GetRateInHz();

        //  Not replaced by a default: a guessed base clock is how a card works on one board and corrupts on another.

        if ((rate < 25000000) || (rate > 1000000000))
        {
            LogError("SD card: implausible controller clock rate from the mailbox: %u Hz\n", rate);
        }

        sd_clock_hz = rate;

        return rate;
    }
}

ExternalMassMediaController &GetExternalMassMediaController()
{
    static ExternalMassMediaController *proxy = nullptr;

    if (proxy == nullptr)
    {
        proxy = static_new<SDCardProxy>();
    }

    return *proxy;
}

void PrepareSDCardPins()
{
    if (GetPlatformInfo().GetEMMCControllerType() == EMMCControllerType::BCM2712_SDHCI)
    {
        return;
    }

    GPIO gpio;

    for (uint8_t pin = 34; pin <= 39; pin++)
    {
        gpio.SetPinFunction(pin, GPIOPinFunction::Input);
    }

    for (uint8_t pin = 48; pin <= 53; pin++)
    {
        gpio.SetPinFunction(pin, GPIOPinFunction::Alt3);
    }
}

unsigned long SDCardDriverArgument()
{
    uint32_t controller = SD_CARD_CONTROLLER_BCM2711;

    switch (GetPlatformInfo().GetEMMCControllerType())
    {
    case EMMCControllerType::BCM2837_ARASAN:
        controller = SD_CARD_CONTROLLER_BCM2837;
        break;

    case EMMCControllerType::BCM2711_EMMC2:
        controller = SD_CARD_CONTROLLER_BCM2711;
        break;

    case EMMCControllerType::BCM2712_SDHCI:
        controller = SD_CARD_CONTROLLER_BCM2712;
        break;
    }

    return SD_CARD_ARGUMENT(controller, ControllerClockHz());
}
