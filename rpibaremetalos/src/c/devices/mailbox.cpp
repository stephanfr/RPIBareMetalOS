// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/mailbox.h"

#include "asm_utility.h"
#include "devices/log.h"

#include "platform/mmu.h"

//extern bool __mmu_enabled;
//extern void *__uncached_memory_base;


bool MailboxPropertyMessage::AddTag(MailboxPropertyMessageTag &tag)
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

bool MailboxPropertyMessage::AddLastTag()
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

void MailboxPropertyMessage::ReturnTags(volatile const char *response_buffer)
{
    volatile const char *current_loc = response_buffer + sizeof(Header);

    for (size_t i = 0; i < num_tags_; i++)
    {
        current_loc += tags_[i]->SetPayload(current_loc);
    }
}

bool Mailbox::sendMessage(MailboxPropertyMessage &message)
{
    const uint32_t MAX_RETRIES = 10000;

    //  Append the 'Last Tag' to the message

    message.AddLastTag();

    uint32_t r = ((uint32_t)(((uint64_t) & (message.AsUint32Buffer()[0])) & 0xFFFFFFF0)) | ((uint32_t)MailboxChannels::PROP & 0x0000000F);

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
        LogError("Timeout waiting for Mailbox to become available\n");
        return false;
    }

    //  Write the address of our message to the mailbox with channel identifier.
    //      If the MMU is not enabled, then we can write the address directly - if the MMU is
    //      enabled, then we need to copy the message to the non-cached block and 
    //      adjust the address of the block so the GPU can see it.

    if( !MemoryManager::IsMMUEnabled() )
    {
        Register(MailboxRegister::WRITE) = r;
    }
    else
    {
        void* uncached_memory_base = MemoryManager::Instance().DMAUncachedMemoryBase();

        memcpy(uncached_memory_base, message.AsUint32Buffer(), MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES);

        uint32_t uncached_r = ((uint32_t)(((uint64_t)(uncached_memory_base)) & 0xFFFFFFF0)) | ((uint32_t)MailboxChannels::PROP & 0x0000000F);

        INSTRUCTION_CACHE_BARRIER;

        Register(MailboxRegister::WRITE) = (uint32_t)reinterpret_cast<uint64_t>(MemoryManager::Instance().ARMToGPUAddress((void*)uncached_r));
//        Register(MailboxRegister::WRITE) = uncached_r;
    }

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
            LogError("Timeout waiting for Mailbox response\n");
            return false;
        }

        //  Loop until we read a response on the property channel.
        //      This *should* be a response to our request.

//        r = Register(MailboxRegister::READ);

        uint32_t result = Register(MailboxRegister::READ);

        unsigned char read_channel = (unsigned char)(result & 0x0F);

        if (read_channel != (unsigned char)MailboxChannels::PROP)
        {
            continue;
        }

        uint32_t *response_message = nullptr;

        if( !MemoryManager::IsMMUEnabled() )
        {
            response_message = reinterpret_cast<uint32_t *>(result & 0xFFFFFFF0);
        }
        else
        {
            response_message = reinterpret_cast<uint32_t *>(GPUaddrToARMaddr(result) & 0xFFFFFFF0);
        }

        printf("Result: 0x%08X, ARM address: 0x%08X, msg: 0x%08X\n", result, GPUaddrToARMaddr(result), response_message);

//__asm volatile ("dc civac, %0" : : "r" (__uncached_memory_base) : "memory");
//__asm volatile ("dsb sy");
//__asm volatile ("isb sy");
//        if( __mmu_enabled )
//        {
//            __asm volatile ("dc civac, %0" : : "r" (__uncached_memory_base) : "memory");
//            memcpy((void*)message.AsUint32Buffer(), __uncached_memory_base, 2048);
//
//            response_message = reinterpret_cast<uint32_t *>((uint64_t)__uncached_memory_base & 0xFFFFFFF0);
//        }

        //  Get a pointer to the new buffer.  This should always be the same address we passed.
        //      The pointer is the upper 28 bits of the return value, so it is implicitly 16 byte aligned.

//        uint32_t *response_message = reinterpret_cast<uint32_t *>(r & 0xFFFFFFF0);

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
            LogError("Mailbox Request Parsing Error\n");
        }
        else
        {
            LogError("Mailbox error, Response Code: %u\n", response_message[1]);
        }

        return false;
    }

    return false;
}
