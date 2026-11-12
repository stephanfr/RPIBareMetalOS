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

    //  The kernel side of a channel whose driver posts to it, or that has to know when its driver is gone.

    class ChannelListener
    {
    public:
        //  A driver's sc_ChannelPost, run in that system call: task context, never more than one at a time.  The
        //      message is the kernel's copy, but its content is the driver's.

        virtual void Posted(const uint8_t *message, size_t length) = 0;

        //  The driver's process is gone and the channel unserved.  On the reaper.

        virtual void Released() {}

    protected:
        ~ChannelListener() = default;
    };
}

//  Gives every channel its memory: the control page and its data area.  Once, at boot, before any driver runs.

void InitializeChannels();

//  For kernel callers: Call() fails with CHANNEL_NO_SERVER while no driver serves the channel.

task::Channel &GetChannel(ChannelId id);

//  At boot, before the channel's driver starts.  A channel without one refuses posts.

void SetChannelListener(ChannelId id, task::ChannelListener *listener);

//  The channel 'handle' names, if 'task' serves it.

bool ServesChannel(const task::TaskImpl &task, uint32_t handle);

//  The system calls, for 'task', the caller.  CHANNEL_* results.

int OpenChannel(task::TaskImpl &task, const char *name, uint32_t &handle, uint64_t &memory, uint64_t &data_size);
int ReceiveOnChannel(task::TaskImpl &task, uint32_t handle, uint64_t timeout_microseconds, size_t &data_length);
int ReplyOnChannel(task::TaskImpl &task, uint32_t handle, uint64_t length, uint64_t data_length);
int PostOnChannel(task::TaskImpl &task, uint32_t handle, const uint8_t *message, size_t length);

//  Unbinds every channel 'process_id' serves, failing the calls it held.  Only once its address space is gone.

void ReleaseChannels(uint32_t process_id);
