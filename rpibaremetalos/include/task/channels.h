// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The kernel's channels to driver processes, and the driver's side of them: the system calls of uapi/channels.h.

#include <stdint.h>

#include "task/basic_channel.h"
#include "task/channel_registry.h"
#include "task/wait_queue.h"

namespace task
{
    class TaskImpl;

    using Channel = BasicChannel<KernelWaitPolicy>;
}

//  Gives every channel its memory: the control page and its data area.  Once, at boot, before any driver runs.

void InitializeChannels();

//  For kernel callers: Call() fails with CHANNEL_NO_SERVER while no driver serves the channel.

task::Channel &GetChannel(ChannelId id);

//  The system calls, for 'task', the caller.  CHANNEL_* results.

int OpenChannel(task::TaskImpl &task, const char *name, uint32_t &handle, uint64_t &memory, uint64_t &data_size);
int ReceiveOnChannel(task::TaskImpl &task, uint32_t handle, uint64_t timeout_microseconds, size_t &data_length);
int ReplyOnChannel(task::TaskImpl &task, uint32_t handle, uint64_t length, uint64_t data_length);

//  Unbinds every channel 'process_id' serves, failing the calls it held.  Only once its address space is gone.

void ReleaseChannels(uint32_t process_id);
