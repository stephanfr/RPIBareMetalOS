// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "mailbox.h"
#include "board.h"

//  Mailbox registers, relative to MAILBOX_BASE_ADDR (see board.h for how
//      that base is derived per-board). These offsets themselves --
//      READ/POLL/SENDER/STATUS/CONFIG/WRITE at +0x00/+0x10/+0x14/+0x18/+0x1C/+0x20 --
//      are unchanged across BCM2835/2711/2712; only the base address moves.

#define MBOX_READ_REG   (*(volatile uint32_t *)(MAILBOX_BASE_ADDR + 0x00))
#define MBOX_STATUS_REG (*(volatile uint32_t *)(MAILBOX_BASE_ADDR + 0x18))
#define MBOX_WRITE_REG  (*(volatile uint32_t *)(MAILBOX_BASE_ADDR + 0x20))

#define MBOX_STATUS_FULL  0x80000000u
#define MBOX_STATUS_EMPTY 0x40000000u

//  Per the mailbox property-interface protocol: bit 31 set means "this is a
//      response"; bit 0 clear/set distinguishes success (0x80000000) from a
//      request-parsing error (0x80000001).

#define MBOX_RESPONSE_SUCCESS 0x80000000u

static inline void data_sync_barrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

int mbox_call(volatile uint32_t *mbox, uint8_t channel)
{
    //  MMU and caches are off for the whole life of this program (see
    //      start.S), so there is no cache-alias bit to set on the pointer --
    //      the plain physical address is exactly what the GPU will read.
    //      The barrier below is what actually guarantees the GPU sees our
    //      writes to 'mbox' before it sees the mailbox write.

    uint32_t message_addr = ((uint32_t)(uintptr_t)mbox & ~0xFu) | (channel & 0xFu);

    data_sync_barrier();

    while (MBOX_STATUS_REG & MBOX_STATUS_FULL)
    {
    }

    MBOX_WRITE_REG = message_addr;

    while (1)
    {
        while (MBOX_STATUS_REG & MBOX_STATUS_EMPTY)
        {
        }

        uint32_t response = MBOX_READ_REG;

        if ((uint8_t)(response & 0xF) != channel)
        {
            continue;
        }

        data_sync_barrier();

        return mbox[1] == MBOX_RESPONSE_SUCCESS;
    }
}

//  Mailbox message buffer. Must be 16-byte aligned per the property-channel
//      protocol (the low 4 bits of the address carry the channel number).
//      36 words is comfortably more than the ~35 words the largest request
//      below actually uses.

static volatile uint32_t mbox_buf[48] __attribute__((aligned(16)));

//  Tag numbers, matching MailboxTags in
//      rpibaremetalos/include/platform/gpu_mailbox_messages.h.

#define MBOX_TAG_SET_PHYS_WH     0x00048003u
#define MBOX_TAG_SET_VIRT_WH     0x00048004u
#define MBOX_TAG_SET_VIRT_OFFSET 0x00048009u
#define MBOX_TAG_SET_DEPTH       0x00048005u
#define MBOX_TAG_SET_PIXEL_ORDER 0x00048006u
#define MBOX_TAG_ALLOCATE_BUFFER 0x00040001u
#define MBOX_TAG_GET_PITCH       0x00040008u
#define MBOX_TAG_GET_PHYS_WH     0x00040003u
#define MBOX_TAG_LAST            0x00000000u

static int fb_get_physical_size(uint32_t *width, uint32_t *height)
{
    int i = 0;

    mbox_buf[i++] = 0; // total size in bytes, fixed up below
    mbox_buf[i++] = 0; // request code

    mbox_buf[i++] = MBOX_TAG_GET_PHYS_WH;
    mbox_buf[i++] = 8; // response is 2 words (width, height)
    mbox_buf[i++] = 0; // request code
    mbox_buf[i++] = 0; // width  (out)
    mbox_buf[i++] = 0; // height (out)

    mbox_buf[i++] = MBOX_TAG_LAST;

    mbox_buf[0] = (uint32_t)(i * 4);

    if (!mbox_call(mbox_buf, MBOX_CH_PROP))
    {
        return 0;
    }

    *width = mbox_buf[5];
    *height = mbox_buf[6];

    return (*width != 0 && *height != 0);
}

int fb_allocate(fb_info_t *out)
{
    uint32_t width;
    uint32_t height;

    if (!fb_get_physical_size(&width, &height))
    {
        //  Fall back to a resolution effectively every HDMI monitor supports.

        width = 1024;
        height = 768;
    }

    int i = 0;

    mbox_buf[i++] = 0; // total size in bytes, fixed up below
    mbox_buf[i++] = 0; // request code

    mbox_buf[i++] = MBOX_TAG_SET_PHYS_WH;
    mbox_buf[i++] = 8;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = width;
    mbox_buf[i++] = height;

    mbox_buf[i++] = MBOX_TAG_SET_VIRT_WH;
    mbox_buf[i++] = 8;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = width;
    mbox_buf[i++] = height;

    mbox_buf[i++] = MBOX_TAG_SET_VIRT_OFFSET;
    mbox_buf[i++] = 8;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = 0;

    //  Always ask for 32bpp directly: a Pi 5 firmware bug (reported against
    //      raspberrypi/firmware, filed against exactly this bare-metal
    //      scenario) silently forces a 16bpp request to 32bpp anyway, so
    //      there's no reason to request anything else.

    mbox_buf[i++] = MBOX_TAG_SET_DEPTH;
    mbox_buf[i++] = 4;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = 32;

    //  1 = RGB. Firmware does not always honor this -- if red/blue look
    //      swapped on real hardware, swap the shifts in fb_rgb() instead of
    //      fighting this tag.

    mbox_buf[i++] = MBOX_TAG_SET_PIXEL_ORDER;
    mbox_buf[i++] = 4;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = 1;

    int allocate_tag_index = i;

    mbox_buf[i++] = MBOX_TAG_ALLOCATE_BUFFER;
    mbox_buf[i++] = 8;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = 4096; // alignment (request); becomes base address (response)
    mbox_buf[i++] = 0;    //                       becomes size (response)

    int pitch_tag_index = i;

    mbox_buf[i++] = MBOX_TAG_GET_PITCH;
    mbox_buf[i++] = 4;
    mbox_buf[i++] = 0;
    mbox_buf[i++] = 0;

    mbox_buf[i++] = MBOX_TAG_LAST;

    mbox_buf[0] = (uint32_t)(i * 4);

    if (!mbox_call(mbox_buf, MBOX_CH_PROP))
    {
        return 0;
    }

    uint32_t fb_addr_raw = mbox_buf[allocate_tag_index + 3];
    uint32_t fb_size = mbox_buf[allocate_tag_index + 4];
    uint32_t pitch = mbox_buf[pitch_tag_index + 3];

    if (fb_addr_raw == 0 || fb_size == 0 || pitch == 0)
    {
        return 0;
    }

    //  The GPU has historically returned a "bus address" with the top bits
    //      used as a cache-alias selector (e.g. 0xC0000000 for an uncached
    //      alias of physical RAM) rather than a plain ARM physical address;
    //      masking them off is the standard convention in Pi3/4 bare-metal
    //      code. This has NOT been confirmed on Pi 5 hardware. If fb_size
    //      and pitch look sane but nothing shows on screen, try using
    //      fb_addr_raw unmasked here first.

    out->framebuffer_addr = fb_addr_raw & 0x3FFFFFFFu;
    out->framebuffer_size = fb_size;
    out->pitch = pitch;
    out->width = width;
    out->height = height;
    out->depth = 32;

    return 1;
}
