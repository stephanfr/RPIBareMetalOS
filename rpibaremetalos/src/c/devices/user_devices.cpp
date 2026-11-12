// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/user_devices.h"

#include <atomic>

#include "devices/log.h"
#include "devices/user_device_registry.h"
#include "platform/exception_manager.h"
#include "platform/platform_info.h"
#include "task/io_doorbell.h"
#include "task/task_impl.h"
#include "uapi/user_devices.h"

namespace
{
    constexpr uint32_t NO_OWNER = 0;            //  process ids start at 1

    //  One per UserDeviceId.  The ISR masks the line and wakes the owner; the owner unmasks it with an ack.

    class UserDevice : public InterruptServiceRoutine
    {
    public:
        UserDevice() = default;

        void SetDescriptor(const UserDeviceDescriptor &descriptor)
        {
            descriptor_ = descriptor;
            present_ = true;
        }

        bool Present() const
        {
            return present_;
        }

        const UserDeviceDescriptor &Descriptor() const
        {
            return descriptor_;
        }

        bool HasInterrupt() const
        {
            return descriptor_.interrupt != Interrupts::NO_SUCH_INTERRUPT;
        }

        uint32_t Owner() const
        {
            return owner_.load(minstd::memory_order_acquire);
        }

        bool OwnedBy(uint32_t process_id) const
        {
            return Owner() == process_id;
        }

        //  False if another process owns it; true, too, if 'process_id' already does.

        bool Claim(uint32_t process_id)
        {
            uint32_t expected = NO_OWNER;

            return owner_.compare_exchange_strong(expected, process_id, minstd::memory_order_acq_rel) || (expected == process_id);
        }

        void Release(uint32_t process_id)
        {
            uint32_t expected = process_id;

            owner_.compare_exchange_strong(expected, NO_OWNER, minstd::memory_order_acq_rel);
        }

        void MaskInterrupt()
        {
            GetExceptionManager().DisableInterrupt(descriptor_.interrupt, CoreList(CoreList::CoreID::CORE0));
        }

        void UnmaskInterrupt()
        {
            GetExceptionManager().EnableInterrupt(descriptor_.interrupt, CoreList(CoreList::CoreID::CORE0));
        }

        task::IODoorbell &Doorbell()
        {
            return doorbell_;
        }

        Interrupts InterruptType() const noexcept override
        {
            return descriptor_.interrupt;
        }

        InterruptServiceRoutineType ISRType() const noexcept override
        {
            return InterruptServiceRoutineType::DEVICE;
        }

        const char *Name() const noexcept override
        {
            return descriptor_.name;
        }

        //  The line stays masked until the owner acks, so a level-triggered device can't storm while its driver
        //      isn't running.

        void HandleInterrupt() override
        {
            MaskInterrupt();
            doorbell_.Ring();
        }

    private:
        UserDeviceDescriptor descriptor_{};
        bool present_ = false;

        minstd::atomic<uint32_t> owner_{NO_OWNER};
        task::IODoorbell doorbell_;
    };

    UserDevice user_devices[static_cast<uint32_t>(UserDeviceId::COUNT)];

    //  The device 'handle' names, if the caller owns it.  Tasks that were never forked have process id 0, the same as
    //      NO_OWNER.

    UserDevice *OwnedDevice(const task::TaskImpl &task, uint32_t handle)
    {
        if ((handle >= static_cast<uint32_t>(UserDeviceId::COUNT)) || !user_devices[handle].Present() || (task.ProcessId() == NO_OWNER) ||
            !user_devices[handle].OwnedBy(task.ProcessId()))
        {
            return nullptr;
        }

        return &user_devices[handle];
    }

    uint64_t DeviceWindow(UserDeviceId id)
    {
        return USER_DEVICE_BASE + (static_cast<uint64_t>(id) * USER_DEVICE_SLOT);
    }
}

void InitializeUserDevices()
{
    const RPIBoardType board = GetPlatformInfo().GetBoardType();

    for (uint32_t id = 0; id < static_cast<uint32_t>(UserDeviceId::COUNT); id++)
    {
        UserDeviceDescriptor descriptor;

        if (!FindUserDevice(board, static_cast<UserDeviceId>(id), descriptor))
        {
            continue;
        }

        UserDevice &device = user_devices[id];

        device.SetDescriptor(descriptor);

        //  Registering enables the line; it stays masked until a driver opens the device.

        if (device.HasInterrupt())
        {
            if (!GetExceptionManager().AddInterruptServiceRoutine(&device, CoreList(CoreList::CoreID::CORE0)))
            {
                LogError("User device '%s': its ISR could not be registered\n", descriptor.name);
            }

            device.MaskInterrupt();
        }
    }
}

int OpenUserDevice(task::TaskImpl &task, const char *name, uint32_t &handle, uint64_t &registers)
{
    UserDeviceDescriptor descriptor;

    if (!FindUserDevice(GetPlatformInfo().GetBoardType(), name, descriptor))
    {
        return DEVICE_NOT_FOUND;
    }

    if ((task.GrantedDevices() & CapabilityFor(descriptor.id)) == 0)
    {
        return DEVICE_NOT_PERMITTED;
    }

    UserDevice &device = user_devices[static_cast<uint32_t>(descriptor.id)];
    const uint32_t process_id = task.ProcessId();
    const bool reopened = device.OwnedBy(process_id);

    if (!device.Claim(process_id))
    {
        return DEVICE_BUSY;
    }

    handle = static_cast<uint32_t>(descriptor.id);
    registers = DeviceWindow(descriptor.id);

    if (reopened)
    {
        return DEVICE_SUCCESS;
    }

    AddressSpace *space = task.UserAddressSpace();

    if ((space == nullptr) ||
        !space->MapPages(registers, descriptor.registers_physical, descriptor.registers_size,
                         Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false, false,
                         MemoryAttribute::DEVICE_NO_GATHER_NO_REORDER_EARLY_WRITE_ACK))
    {
        device.Release(process_id);
        return DEVICE_NO_MEMORY;
    }

    if (device.HasInterrupt())
    {
        device.Doorbell().Discard();
        device.UnmaskInterrupt();
    }

    return DEVICE_SUCCESS;
}

int WaitForUserDeviceInterrupt(task::TaskImpl &task, uint32_t handle, uint64_t timeout_microseconds)
{
    UserDevice *device = OwnedDevice(task, handle);

    if (device == nullptr)
    {
        return DEVICE_INVALID_HANDLE;
    }

    if (!device->HasInterrupt())
    {
        return DEVICE_NO_INTERRUPT;
    }

    if (timeout_microseconds == DEVICE_WAIT_FOREVER)
    {
        device->Doorbell().Wait(task::WaitReason::DEVICE);
        return DEVICE_SUCCESS;
    }

    //  Clamped so a value past INT64_MAX can't turn negative.

    const uint64_t clamped = (timeout_microseconds > static_cast<uint64_t>(INT64_MAX)) ? static_cast<uint64_t>(INT64_MAX) : timeout_microseconds;

    return device->Doorbell().Wait(task::WaitReason::DEVICE, microseconds(static_cast<int64_t>(clamped))) ? DEVICE_SUCCESS : DEVICE_TIMED_OUT;
}

int AcknowledgeUserDeviceInterrupt(task::TaskImpl &task, uint32_t handle)
{
    UserDevice *device = OwnedDevice(task, handle);

    if (device == nullptr)
    {
        return DEVICE_INVALID_HANDLE;
    }

    if (!device->HasInterrupt())
    {
        return DEVICE_NO_INTERRUPT;
    }

    device->UnmaskInterrupt();

    return DEVICE_SUCCESS;
}

void ReleaseUserDevices(uint32_t process_id)
{
    for (UserDevice &device : user_devices)
    {
        if (!device.Present() || !device.OwnedBy(process_id))
        {
            continue;
        }

        if (device.HasInterrupt())
        {
            device.MaskInterrupt();
        }

        device.Release(process_id);
    }
}

uint32_t UserDeviceOwner(UserDeviceId id)
{
    return (id < UserDeviceId::COUNT) ? user_devices[static_cast<uint32_t>(id)].Owner() : 0;
}
