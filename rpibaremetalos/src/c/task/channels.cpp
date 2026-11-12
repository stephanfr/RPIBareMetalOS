// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/channels.h"

#include <string.h>

#include "devices/log.h"
#include "platform/address_space_layout.h"
#include "platform/memory_model.h"
#include "task/task_impl.h"
#include "uapi/channels.h"
#include "uapi/console.h"
#include "uapi/hw_rng.h"
#include "uapi/sd_card.h"

namespace
{
    constexpr const char *CHANNEL_NAMES[] = {CHANNEL_NAME_TEST_ECHO, CHANNEL_NAME_HW_RNG, CHANNEL_NAME_CONSOLE, CHANNEL_NAME_SD_CARD};

    static_assert(sizeof(CHANNEL_NAMES) / sizeof(CHANNEL_NAMES[0]) == static_cast<uint32_t>(ChannelId::COUNT), "a name per channel");
    static_assert(static_cast<uint64_t>(ChannelId::COUNT) * USER_CHANNEL_SLOT <= USER_CHANNEL_TOP - USER_CHANNEL_BASE, "a slot per channel");
    static_assert(CHANNEL_DATA_OFFSET + CHANNEL_DATA_MAX <= USER_CHANNEL_SLOT, "a channel's memory fits its slot");

    constexpr bool DataSizesAreValid()
    {
        for (uint32_t id = 0; id < static_cast<uint32_t>(ChannelId::COUNT); id++)
        {
            const size_t size = ChannelDataSize(static_cast<ChannelId>(id));

            if ((size > CHANNEL_DATA_MAX) || ((size % CHANNEL_CONTROL_SIZE) != 0))
            {
                return false;
            }
        }

        return true;
    }

    static_assert(DataSizesAreValid(), "a channel's data area is whole pages, at most CHANNEL_DATA_MAX");

    task::Channel channels[static_cast<uint32_t>(ChannelId::COUNT)];
    task::ChannelListener *listeners[static_cast<uint32_t>(ChannelId::COUNT)] = {};
    uint64_t channel_memory[static_cast<uint32_t>(ChannelId::COUNT)];       //  physical; 0 if it has none

    size_t MemorySize(ChannelId id)
    {
        return CHANNEL_DATA_OFFSET + ChannelDataSize(id);
    }

    bool FindChannel(const char *name, ChannelId &id)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(ChannelId::COUNT); i++)
        {
            if (strncmp(name, CHANNEL_NAMES[i], CHANNEL_NAME_LENGTH) == 0)
            {
                id = static_cast<ChannelId>(i);
                return true;
            }
        }

        return false;
    }

    //  The channel 'handle' names, if the caller serves it.  Tasks that were never forked have process id 0, the same as
    //      an unserved channel's.

    task::Channel *ServedChannel(const task::TaskImpl &task, uint32_t handle)
    {
        if ((handle >= static_cast<uint32_t>(ChannelId::COUNT)) || (task.ProcessId() == 0) || (channels[handle].Server() != task.ProcessId()))
        {
            return nullptr;
        }

        return &channels[handle];
    }

    uint64_t ChannelWindow(ChannelId id)
    {
        return USER_CHANNEL_BASE + (static_cast<uint64_t>(id) * USER_CHANNEL_SLOT);
    }
}

void InitializeChannels()
{
    for (uint32_t id = 0; id < static_cast<uint32_t>(ChannelId::COUNT); id++)
    {
        const size_t size = MemorySize(static_cast<ChannelId>(id));
        MemoryPagePointer memory = MemoryModel::AllocateUserFrame(size);

        if (memory == 0)
        {
            LogError("Channel '%s': no memory for it\n", CHANNEL_NAMES[id]);
            continue;
        }

        memset(static_cast<uint8_t *>(memory), 0, size);

        channel_memory[id] = memory.Physical();
        channels[id].SetMemory(static_cast<uint8_t *>(memory), ChannelDataSize(static_cast<ChannelId>(id)));
    }
}

task::Channel &GetChannel(ChannelId id)
{
    return channels[static_cast<uint32_t>(id)];
}

void SetChannelListener(ChannelId id, task::ChannelListener *listener)
{
    if (id < ChannelId::COUNT)
    {
        listeners[static_cast<uint32_t>(id)] = listener;
    }
}

bool ServesChannel(const task::TaskImpl &task, uint32_t handle)
{
    return ServedChannel(task, handle) != nullptr;
}

int OpenChannel(task::TaskImpl &task, const char *name, uint32_t &handle, uint64_t &memory, uint64_t &data_size)
{
    ChannelId id;

    if (!FindChannel(name, id))
    {
        return CHANNEL_NOT_FOUND;
    }

    if ((task.GrantedChannels() & CapabilityFor(id)) == 0)
    {
        return CHANNEL_NOT_PERMITTED;
    }

    task::Channel &channel = channels[static_cast<uint32_t>(id)];
    const uint32_t process_id = task.ProcessId();
    const bool reopened = (channel.Server() == process_id);

    if (channel_memory[static_cast<uint32_t>(id)] == 0)
    {
        return CHANNEL_NO_MEMORY;
    }

    if (!channel.Bind(process_id))
    {
        return CHANNEL_BUSY;
    }

    handle = static_cast<uint32_t>(id);
    memory = ChannelWindow(id);
    data_size = channel.DataSize();

    if (reopened)
    {
        return CHANNEL_SUCCESS;
    }

    AddressSpace *space = task.UserAddressSpace();

    if ((space == nullptr) ||
        !space->MapPages(memory, channel_memory[static_cast<uint32_t>(id)], MemorySize(id),
                         Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false, false))
    {
        channel.Unbind(process_id);
        return CHANNEL_NO_MEMORY;
    }

    return CHANNEL_SUCCESS;
}

int ReceiveOnChannel(task::TaskImpl &task, uint32_t handle, uint64_t timeout_microseconds, size_t &data_length)
{
    task::Channel *channel = ServedChannel(task, handle);

    if (channel == nullptr)
    {
        return CHANNEL_INVALID_HANDLE;
    }

    if (timeout_microseconds == CHANNEL_WAIT_FOREVER)
    {
        return channel->Receive(data_length);
    }

    //  Clamped so a value past INT64_MAX can't turn negative.

    const uint64_t clamped = (timeout_microseconds > static_cast<uint64_t>(INT64_MAX)) ? static_cast<uint64_t>(INT64_MAX) : timeout_microseconds;

    return channel->Receive(microseconds(static_cast<int64_t>(clamped)), data_length);
}

int ReplyOnChannel(task::TaskImpl &task, uint32_t handle, uint64_t length, uint64_t data_length)
{
    task::Channel *channel = ServedChannel(task, handle);

    if (channel == nullptr)
    {
        return CHANNEL_INVALID_HANDLE;
    }

    if ((length > CHANNEL_MESSAGE_MAX) || (data_length > CHANNEL_DATA_MAX))
    {
        return CHANNEL_TOO_LONG;
    }

    return channel->Reply(static_cast<size_t>(length), static_cast<size_t>(data_length));
}

int PostOnChannel(task::TaskImpl &task, uint32_t handle, const uint8_t *message, size_t length)
{
    if (ServedChannel(task, handle) == nullptr)
    {
        return CHANNEL_INVALID_HANDLE;
    }

    if (listeners[handle] == nullptr)
    {
        return CHANNEL_NO_LISTENER;
    }

    if (length > CHANNEL_POST_MAX)
    {
        return CHANNEL_TOO_LONG;
    }

    listeners[handle]->Posted(message, length);

    return CHANNEL_SUCCESS;
}

void ReleaseChannels(uint32_t process_id)
{
    for (uint32_t id = 0; id < static_cast<uint32_t>(ChannelId::COUNT); id++)
    {
        const bool served = (process_id != 0) && (channels[id].Server() == process_id);

        channels[id].Unbind(process_id);

        if (served && (listeners[id] != nullptr))
        {
            listeners[id]->Released();
        }
    }
}
