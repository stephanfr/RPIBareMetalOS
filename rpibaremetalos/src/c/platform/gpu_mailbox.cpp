// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/gpu_mailbox.h"

#include "asm_utility.h"
#include "devices/physical_timer.h"
#include "devices/log.h"

#include "platform/mmu_manager.h"

bool GPUMailboxPropertyMessage::AddTag(GPUMailboxPropertyMessageTag &tag)
{
    if ((num_tags_ >= MAX_TAGS_PER_MESSAGE) ||
        (message_buffer_.header_.buffer_size_ + tag.GetPayloadSize() >= MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES))
    {
        return false;
    }

    memcpy((void *)(message_buffer_.buffer_ + message_buffer_.header_.buffer_size_), tag.GetPayload(), tag.GetPayloadSize());
    message_buffer_.header_.buffer_size_ += tag.GetPayloadSize();

    tags_[num_tags_++] = &tag;

    return true;
}

bool GPUMailboxPropertyMessage::AddLastTag()
{
    if (message_buffer_.header_.buffer_size_ + sizeof(uint32_t) >= MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES)
    {
        return false;
    }

    uint32_t closing_tag = (uint32_t)MailboxTags::LAST;

    memcpy((void *)(message_buffer_.buffer_ + message_buffer_.header_.buffer_size_), (void *)&closing_tag, sizeof(uint32_t));
    message_buffer_.header_.buffer_size_ += sizeof(uint32_t);

    return true;
}

void GPUMailboxPropertyMessage::ReturnTags(volatile const char *response_buffer)
{
    volatile const char *current_loc = response_buffer + sizeof(Header);

    for (size_t i = 0; i < num_tags_; i++)
    {
        current_loc += tags_[i]->SetPayload(current_loc);
    }
}

namespace
{
    //  Wall-clock timeouts, not spin counts. The same iteration count is a
    //      very different amount of real time on a 1.2GHz Pi 3 versus a
    //      1.5GHz Pi 4 or a Pi 5. ALLOCATE_FRAMEBUFFER, which
    //      makes the firmware allocate GPU memory and potentially reprogram
    //      the HDMI output -- routinely tens of milliseconds, so a simple
    //      looping retry counter is not sufficient. The GPU may also be busy with
    //      other work, so we need to wait for a response for a generous amount of time.

    constexpr uint64_t MAILBOX_AVAILABLE_TIMEOUT_IN_MICROSECONDS = 100000;  //  100ms
    constexpr uint64_t MAILBOX_RESPONSE_TIMEOUT_IN_MICROSECONDS = 2000000;  //  2s -- generous, this is a boot-time path

    uint64_t TimeoutInTimerTicks(uint64_t timeout_in_microseconds)
    {
        uint64_t counter_frequency;

        asm volatile("mrs %0, cntfrq_el0" : "=r"(counter_frequency));

        uint64_t ticks_per_microsecond = counter_frequency / 1000000;

        //  Guard against a zero/unset counter frequency leaving us with a
        //      zero-length deadline, which would time out instantly.

        if (ticks_per_microsecond == 0)
        {
            ticks_per_microsecond = 1;
        }

        return ticks_per_microsecond * timeout_in_microseconds;
    }
}

bool GPUMailbox::sendMessage(GPUMailboxPropertyMessage &message)
{
    //  Append the 'Last Tag' to the message

    message.AddLastTag();

    //  Wait until we can write to the mailbox

    uint64_t deadline = PhysicalTimer::CurrentTicks() + TimeoutInTimerTicks(MAILBOX_AVAILABLE_TIMEOUT_IN_MICROSECONDS);

    while (Register(MailboxRegister::STATUS) & MBOX_STATUS_FULL)
    {
        if (PhysicalTimer::CurrentTicks() >= deadline)
        {
            LogError("Timeout waiting for GPUMailbox to become available\n");
            return false;
        }

        CPUTicksDelay(50); //  Ease off the peripheral bus between polls
    }

    //  Write the address of our message to the mailbox with channel identifier.
    //      The MMU is enabled, so we need to copy the message to the non-cached block and
    //      adjust the address of the block so the GPU can see it.
    //
    //  DSB ensures all prior writes (the memcpy) are globally visible before the GPU reads
    //      the buffer. ISB is insufficient here — it only flushes the instruction pipeline.

    void *uncached_memory_base = MMUManager::Instance().DMAUncachedMemoryBase();

    memcpy(uncached_memory_base, message.AsUint32Buffer(), MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES);

    void *message_ARM_address = reinterpret_cast<void *>(((uint64_t)(uncached_memory_base) & 0xFFFFFFFFFFFFFFF0) | ((uint64_t)MailboxChannels::PROP & 0x000000000000000F));

    asm volatile("dsb sy" ::: "memory");

    Register(MailboxRegister::WRITE) = (uint32_t) reinterpret_cast<uint64_t>(MMUManager::Instance().ARMToGPUAddress(message_ARM_address));

    //  Now wait for the response. The deadline covers the whole wait, including
    //      any messages we read and discard from other channels -- unlike the
    //      previous retry counter, which was never reset across those and so
    //      shrank the remaining budget each time one arrived.

    deadline = PhysicalTimer::CurrentTicks() + TimeoutInTimerTicks(MAILBOX_RESPONSE_TIMEOUT_IN_MICROSECONDS);

    while (true)
    {
        //  Wait for a response

        while (Register(MailboxRegister::STATUS) & MBOX_STATUS_EMPTY)
        {
            if (PhysicalTimer::CurrentTicks() >= deadline)
            {
                LogError("Timeout waiting for GPUMailbox response\n");
                return false;
            }

            CPUTicksDelay(50);
        }

        //  Loop until we read a response on the property channel.
        //      This *should* be a response to our request.

        uint32_t result = Register(MailboxRegister::READ);

        unsigned char read_channel = (unsigned char)(result & 0x0F);

        if (read_channel != (unsigned char)MailboxChannels::PROP)
        {
            continue;
        }

        //  Get a pointer to the new buffer.  This should always be the same address we passed to the GPU.
        //      The pointer is the upper 28 bits of the return value, so it is implicitly 16 byte aligned.

        uint32_t *response_message = reinterpret_cast<uint32_t *>(reinterpret_cast<uint64_t>(message_ARM_address) & 0xFFFFFFFFFFFFFFF0);

        if (response_message[1] == MBOX_STATUS_RESPONSE_SUCCESS)
        {
            message.ReturnTags((const char *)response_message);

            message.Reset();

            return true;
        }

        if (response_message[1] == MBOX_STATUS_REQUEST_PARSING_ERROR)
        {
            LogError("GPUMailbox Request Parsing Error\n");
        }
        else
        {
            LogError("GPUMailbox error, Response Code: %u\n", response_message[1]);
        }

        return false;
    }

    return false;
}
