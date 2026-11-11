// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_mailbox.h"

#include "hypervisor_config.h"

#define HEADER_WORDS 2 //  size, request/response code
#define TAG_HEADER_WORDS 3 //  tag, value buffer size, request/response code
#define END_TAG 0u
#define CODE_REQUEST 0u
#define TAG_CODE_RESPONSE (1u << 31)
#define MIN_MESSAGE_SIZE ((HEADER_WORDS + 1) * 4)

//  The tags the kernel sends once stage-2 is on, and the harmless reads beside them.  Allocating a framebuffer, and
//      the settings that can reallocate it, are left out: the framebuffer is mapped once, at boot, and a new one would
//      land in VideoCore memory, which VM 1 can't reach.

static const uint32_t ALLOWED_TAGS[] = {
    0x00000001, //  firmware version
    0x00010001, //  board model
    0x00010002, //  board revision
    0x00010003, //  MAC address
    0x00010004, //  serial number
    0x00010005, //  ARM memory
    0x00010006, //  VideoCore memory
    0x00020001, //  get power state
    0x00028001, //  set power state
    0x00030002, //  get clock rate
    0x00038002, //  set clock rate
    0x00030048, //  notify reboot
    0x00040003, //  get physical width and height
    0x00040008, //  get pitch
    0x00048009, //  set virtual offset: moves the view within the framebuffer, never reallocates it
    0x00050001, //  command line
};

uint32_t HypervisorMailboxTagAllowed(uint32_t tag)
{
    for (uint32_t i = 0; i < sizeof(ALLOWED_TAGS) / sizeof(ALLOWED_TAGS[0]); i++)
    {
        if (ALLOWED_TAGS[i] == tag)
        {
            return 1;
        }
    }

    return 0;
}

int64_t HypervisorMailboxCheck(const volatile uint32_t *message, uint32_t size)
{
    if ((size < MIN_MESSAGE_SIZE) || (size > HYPERVISOR_MAILBOX_MESSAGE_MAX) || ((size % 4) != 0) ||
        (message[0] != size) || (message[1] != CODE_REQUEST))
    {
        return HVC_INVALID_PARAMETER;
    }

    const uint32_t words = size / 4;
    uint32_t index = HEADER_WORDS;

    while (index < words)
    {
        const uint32_t tag = message[index];

        if (tag == END_TAG)
        {
            return HVC_SUCCESS;
        }

        if ((words - index < TAG_HEADER_WORDS) || !HypervisorMailboxTagAllowed(tag) ||
            ((message[index + 2] & TAG_CODE_RESPONSE) != 0))
        {
            return HVC_INVALID_PARAMETER;
        }

        //  The firmware writes a tag's response into its value buffer, so the buffer must lie inside the message,
        //      with at least the end tag after it.  64-bit, so a size near 4 GB can't wrap to a small one.

        const uint64_t value_words = ((uint64_t)message[index + 1] + 3) / 4;

        if (value_words >= (uint64_t)(words - index - TAG_HEADER_WORDS))
        {
            return HVC_INVALID_PARAMETER;
        }

        index += TAG_HEADER_WORDS + (uint32_t)value_words;
    }

    return HVC_INVALID_PARAMETER;
}
