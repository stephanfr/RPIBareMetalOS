// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/driver_manager.h"

#include <atomic>
#include <fixed_string>

#include "devices/log.h"
#include "devices/physical_timer.h"
#include "devices/user_device_registry.h"
#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/stats_db.h"
#include "task/channel_registry.h"
#include "task/io_doorbell.h"
#include "task/runnable.h"
#include "task/task_impl.h"
#include "task/tasks.h"

namespace
{
    using minstd::chrono::duration_cast;
    using minstd::chrono::milliseconds;
    using minstd::chrono::nanoseconds;
    using minstd::chrono::time_point;

    constexpr milliseconds FIRST_RESTART_DELAY(100);
    constexpr milliseconds MAX_RESTART_DELAY(10000);

    //  A driver that ran this long before exiting starts again from FIRST_RESTART_DELAY.

    constexpr milliseconds STABLE_RUN(30000);

    struct DriverDefinition
    {
        const char *name;
        const char *image;                  //  a loader path: "embedded:<name>" for an image in the kernel
        unsigned long arg;
        DeviceCapabilities devices;
        ChannelCapabilities channels;
        bool start_at_boot;
    };

    constexpr DriverDefinition DRIVERS[] = {
        {"test-echo", "embedded:hello.bin", 19, NO_DEVICE_CAPABILITIES, CapabilityFor(ChannelId::TEST_ECHO), false},
    };

    static_assert(sizeof(DRIVERS) / sizeof(DRIVERS[0]) == static_cast<uint32_t>(DriverId::COUNT), "a definition per driver");

    system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::DRIVER_STARTS>
        driver_starts("drivers", "starts", "EL0 driver processes started, restarts included", 0);

    system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::DRIVER_RESTARTS>
        driver_restarts("drivers", "restarts", "EL0 driver processes started again after exiting", 0);

    //  wanted_ and released_ are written by other tasks; the rest only by the manager task.  Status readers may see a
    //      slot mid-update, which costs them nothing worse than a stale count.

    struct DriverSlot
    {
        minstd::atomic<bool> wanted_{false};
        minstd::atomic<bool> released_{false};
        minstd::atomic<uint32_t> process_id_{0};
        minstd::atomic<uint32_t> state_{static_cast<uint32_t>(DriverState::STOPPED)};
        minstd::atomic<uint32_t> starts_{0};
        minstd::atomic<uint32_t> exits_{0};
        minstd::atomic<uint32_t> last_restart_delay_ms_{0};

        milliseconds next_delay_{FIRST_RESTART_DELAY};
        time_point<nanoseconds> started_at_;
        time_point<nanoseconds> restart_at_;

        DriverState State() const
        {
            return static_cast<DriverState>(state_.load(minstd::memory_order_acquire));
        }

        void SetState(DriverState state)
        {
            state_.store(static_cast<uint32_t>(state), minstd::memory_order_release);
        }
    };

    DriverSlot slots[static_cast<uint32_t>(DriverId::COUNT)];

    task::IODoorbell manager_doorbell;

    class DriverManagerTask : public Runnable
    {
    public:
        void Run() override
        {
            for (uint32_t id = 0; id < static_cast<uint32_t>(DriverId::COUNT); id++)
            {
                if (DRIVERS[id].start_at_boot)
                {
                    slots[id].wanted_.store(true, minstd::memory_order_release);
                }
            }

            for (;;)
            {
                const auto now = PhysicalTimer::Now();
                bool timed = false;
                time_point<nanoseconds> next_wake;

                for (uint32_t id = 0; id < static_cast<uint32_t>(DriverId::COUNT); id++)
                {
                    Step(id, now);

                    if ((slots[id].State() == DriverState::RESTART_PENDING) && (!timed || (slots[id].restart_at_ < next_wake)))
                    {
                        timed = true;
                        next_wake = slots[id].restart_at_;
                    }
                }

                if (!timed)
                {
                    manager_doorbell.Wait(task::WaitReason::EVENT);
                    continue;
                }

                const auto remaining = next_wake - PhysicalTimer::Now();

                if (remaining.count() > 0)
                {
                    manager_doorbell.Wait(task::WaitReason::EVENT, duration_cast<minstd::chrono::microseconds>(remaining) + minstd::chrono::microseconds(1));
                }
            }
        }

    private:
        static void Step(uint32_t id, time_point<nanoseconds> now)
        {
            DriverSlot &slot = slots[id];
            const DriverDefinition &driver = DRIVERS[id];
            const bool wanted = slot.wanted_.load(minstd::memory_order_acquire);

            switch (slot.State())
            {
            case DriverState::STOPPED:
                if (wanted)
                {
                    Launch(id, now);
                }
                break;

            case DriverState::RUNNING:
                if (!slot.released_.load(minstd::memory_order_acquire))
                {
                    break;
                }

                slot.exits_.fetch_add(1);

                if (!wanted)
                {
                    LogInfo("Driver '%s' (process %u) exited, and stays stopped\n", driver.name, slot.process_id_.load());
                    slot.process_id_.store(0);
                    slot.SetState(DriverState::STOPPED);
                    break;
                }

                if (duration_cast<milliseconds>(now - slot.started_at_) >= STABLE_RUN)
                {
                    slot.next_delay_ = FIRST_RESTART_DELAY;
                }

                LogError("Driver '%s' (process %u) exited: restarting in %ld ms\n", driver.name, slot.process_id_.load(), slot.next_delay_.count());

                slot.process_id_.store(0);
                ScheduleRestart(slot, now);
                break;

            case DriverState::RESTART_PENDING:
                if (!wanted)
                {
                    slot.SetState(DriverState::STOPPED);
                }
                else if (now >= slot.restart_at_)
                {
                    driver_restarts.Increment();
                    Launch(id, now);
                }
                break;
            }
        }

        static void ScheduleRestart(DriverSlot &slot, time_point<nanoseconds> now)
        {
            slot.restart_at_ = now + slot.next_delay_;
            slot.last_restart_delay_ms_.store(static_cast<uint32_t>(slot.next_delay_.count()));
            slot.next_delay_ = (slot.next_delay_ * 2 < MAX_RESTART_DELAY) ? slot.next_delay_ * 2 : MAX_RESTART_DELAY;
            slot.SetState(DriverState::RESTART_PENDING);
        }

        static void Launch(uint32_t id, time_point<nanoseconds> now)
        {
            DriverSlot &slot = slots[id];
            const DriverDefinition &driver = DRIVERS[id];

            minstd::fixed_string<MAX_FILENAME_LENGTH> image(driver.image);

            slot.released_.store(false, minstd::memory_order_release);

            auto forked = task::GetTaskManager().ForkServiceTask(image, driver.arg, task::TaskDefinition{driver.name}, driver.devices, driver.channels);

            uint32_t process_id = 0;

            if (forked.Successful())
            {
                auto task = task::GetTaskManager().FindTask(forked.Value());

                if (task.has_value())
                {
                    process_id = static_cast<task::TaskImpl &>(*task).ProcessId();
                }
            }

            if (process_id == 0)
            {
                LogError("Driver '%s' could not be started: retrying in %ld ms\n", driver.name, slot.next_delay_.count());
                ScheduleRestart(slot, now);
                return;
            }

            driver_starts.Increment();

            slot.started_at_ = now;
            slot.starts_.fetch_add(1);
            slot.process_id_.store(process_id, minstd::memory_order_release);
            slot.SetState(DriverState::RUNNING);
        }
    };

    DriverManagerTask manager_task;
}

bool StartDriverManager()
{
    auto forked = task::GetTaskManager().ForkKernelTask(&manager_task, task::TaskDefinition{"driver manager"});

    if (forked.Failed())
    {
        LogError("The driver manager could not be started\n");
        return false;
    }

    return true;
}

void StartDriver(DriverId id)
{
    if (id < DriverId::COUNT)
    {
        slots[static_cast<uint32_t>(id)].wanted_.store(true, minstd::memory_order_release);
        manager_doorbell.Ring();
    }
}

void StopDriver(DriverId id)
{
    if (id < DriverId::COUNT)
    {
        slots[static_cast<uint32_t>(id)].wanted_.store(false, minstd::memory_order_release);
        manager_doorbell.Ring();
    }
}

DriverStatus GetDriverStatus(DriverId id)
{
    if (id >= DriverId::COUNT)
    {
        return DriverStatus{"", DriverState::STOPPED, 0, 0, 0, 0};
    }

    const DriverSlot &slot = slots[static_cast<uint32_t>(id)];

    return DriverStatus{DRIVERS[static_cast<uint32_t>(id)].name, slot.State(), slot.process_id_.load(), slot.starts_.load(),
                        slot.exits_.load(), slot.last_restart_delay_ms_.load()};
}

const char *ToString(DriverState state)
{
    switch (state)
    {
    case DriverState::STOPPED:
        return "stopped";
    case DriverState::RUNNING:
        return "running";
    case DriverState::RESTART_PENDING:
        return "restart pending";
    default:
        return "unknown";
    }
}

void NotifyProcessReleased(uint32_t process_id)
{
    if (process_id == 0)
    {
        return;
    }

    for (DriverSlot &slot : slots)
    {
        if (slot.process_id_.load(minstd::memory_order_acquire) == process_id)
        {
            slot.released_.store(true, minstd::memory_order_release);
            manager_doorbell.Ring();
        }
    }
}
