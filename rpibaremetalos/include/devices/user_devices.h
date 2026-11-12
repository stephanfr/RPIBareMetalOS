// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The kernel side of the device system calls (uapi/user_devices.h): which process owns each device of this board's
//      registry, and the interrupt forwarding to it.

#include <stdint.h>

#include "devices/user_device_registry.h"

namespace task
{
    class TaskImpl;
}

//  Registers an ISR, line masked, for every device on this board that has an interrupt.  Once, at boot, with the
//      other ISRs: the ISR map isn't locked against concurrent dispatch.

void InitializeUserDevices();

//  The system calls, for 'task', the caller.  DEVICE_* results.

int OpenUserDevice(task::TaskImpl &task, const char *name, uint32_t &handle, uint64_t &registers);
int WaitForUserDeviceInterrupt(task::TaskImpl &task, uint32_t handle, uint64_t timeout_microseconds);
int AcknowledgeUserDeviceInterrupt(task::TaskImpl &task, uint32_t handle);
int RouteUserDeviceInterrupt(task::TaskImpl &task, uint32_t device_handle, uint32_t channel_handle);

//  Gives back every device 'process_id' owns, its interrupt masked.  Only once its address space is gone, so the
//      next owner is the only process mapping the registers.  Repeating it is harmless.

void ReleaseUserDevices(uint32_t process_id);

//  The process that owns the device, or 0.

uint32_t UserDeviceOwner(UserDeviceId id);
