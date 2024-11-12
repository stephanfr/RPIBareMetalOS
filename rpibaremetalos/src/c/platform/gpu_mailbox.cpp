// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/gpu_mailbox.h"

#include "asm_utility.h"
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

bool GPUMailbox::sendMessage(GPUMailboxPropertyMessage &message)
{
    const uint32_t MAX_RETRIES = 10000;

    //  Append the 'Last Tag' to the message

    message.AddLastTag();

    //  Wait until we can write to the mailbox

    uint32_t retries = 0;

    do
    {
        CPUTicksDelay(50);
        retries++;
    } while ((Register(MailboxRegister::STATUS) & MBOX_STATUS_FULL) && (retries < MAX_RETRIES));

    //  Error if we have exceeded the max retries

    if (retries >= MAX_RETRIES)
    {
        LogError("Timeout waiting for GPUMailbox to become available\n");
        return false;
    }

    //  Write the address of our message to the mailbox with channel identifier.
    //      The MMU is enabled, so we need to copy the message to the non-cached block and
    //      adjust the address of the block so the GPU can see it.
    //
    //  Insert an instruction cache barrier to ensure the data is visible to the GPU, as
    //    instructions *could* be reordered such that the write to the register occurs before
    //    the data is written to the uncached memory.

    void *uncached_memory_base = MMUManager::Instance().DMAUncachedMemoryBase();

    memcpy(uncached_memory_base, message.AsUint32Buffer(), MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES);

    void *message_ARM_address = reinterpret_cast<void *>(((uint64_t)(uncached_memory_base) & 0xFFFFFFFFFFFFFFF0) | ((uint64_t)MailboxChannels::PROP & 0x000000000000000F));

    INSTRUCTION_CACHE_BARRIER;

    Register(MailboxRegister::WRITE) = (uint32_t) reinterpret_cast<uint64_t>(MMUManager::Instance().ARMToGPUAddress(message_ARM_address));

    //  Now wait for the response.  Timeout if we have to wait too long.

    retries = 0;

    while (true)
    {
        //  Wait for a response

        do
        {
            CPUTicksDelay(50);
            retries++;
        } while ((Register(MailboxRegister::STATUS) & MBOX_STATUS_EMPTY) && (retries < MAX_RETRIES));

        //  Error if we have exceeded the max retries

        if (retries >= MAX_RETRIES)
        {
            LogError("Timeout waiting for GPUMailbox response\n");
            return false;
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
            //  If it is a successful response, then return the tag responses and reset the message so it can be re-used.
            //      We ought to exit here 99.9999% of the time.

            message.ReturnTags((const char *)response_message);

            message.Reset();

            return true;
        }

        //  It was an unsuccessful response, usually it will be a parsing error.
        //      Parsing errors seem to be associated with a mismatch between the request and
        //      reply structures and the requested operation.

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
