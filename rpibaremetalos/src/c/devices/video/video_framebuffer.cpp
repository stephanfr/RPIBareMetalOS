// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/video/video_framebuffer.h"

#include "devices/log.h"     //  TEMPORARY -- remove once framebuffer diagnosis is complete

bool VideoFrameBuffer::Allocate()
{
    GPUMailbox mbox;

    //  Query whatever physical resolution firmware already negotiated
    //      with the monitor; fall back if that fails.

    GetPhysicalWidthHeightTag getPhysicalWidthHeightTag;
    GPUMailboxPropertyMessage getPhysicalSizeMessage(getPhysicalWidthHeightTag);

    uint32_t requested_width = DEFAULT_FALLBACK_WIDTH;
    uint32_t requested_height = DEFAULT_FALLBACK_HEIGHT;

    bool query_ok = mbox.sendMessage(getPhysicalSizeMessage);

    LogError("FB DIAG: physical-size query sendMessage=%u W=%u H=%u\n",
             query_ok, getPhysicalWidthHeightTag.GetWidth(), getPhysicalWidthHeightTag.GetHeight());

    if (query_ok &&
        (getPhysicalWidthHeightTag.GetWidth() != 0) &&
        (getPhysicalWidthHeightTag.GetHeight() != 0))
    {
        requested_width = getPhysicalWidthHeightTag.GetWidth();
        requested_height = getPhysicalWidthHeightTag.GetHeight();
    }

    LogError("FB DIAG: requesting %ux%u\n", requested_width, requested_height);

    //  One combined message: set physical/virtual size, offset, depth,
    //      pixel order, allocate the buffer, and read back the pitch.

    SetPhysicalWidthHeightTag setPhysicalWidthHeightTag(requested_width, requested_height);
    SetVirtualWidthHeightTag setVirtualWidthHeightTag(requested_width, requested_height);
    SetVirtualOffsetTag setVirtualOffsetTag(0, 0);
    SetColourDepthTag setColourDepthTag(DEPTH_BITS_PER_PIXEL);
    SetPixelOrderTag setPixelOrderTag(FrameBufferPixelOrder::RGB);
    AllocateFrameBufferTag allocateFrameBufferTag(ALLOCATE_ALIGNMENT_BYTES);
    GetPitchTag getPitchTag;

    GPUMailboxPropertyMessage allocateMessage(setPhysicalWidthHeightTag,
                                              setVirtualWidthHeightTag,
                                              setVirtualOffsetTag,
                                              setColourDepthTag,
                                              setPixelOrderTag,
                                              allocateFrameBufferTag,
                                              getPitchTag);

    bool allocate_ok = mbox.sendMessage(allocateMessage);

    LogError("FB DIAG: allocate sendMessage=%u\n", allocate_ok);

    if (!allocate_ok)
    {
        return false;
    }

    //  Per the mailbox docs (and the comments on these tags): read back
    //      what was actually applied, never trust the request. This is
    //      the exact fix for the stretched/wrong-aspect-ratio bug found
    //      on real Pi 5 hardware.

    uint32_t applied_width = setPhysicalWidthHeightTag.GetAppliedWidth();
    uint32_t applied_height = setPhysicalWidthHeightTag.GetAppliedHeight();
    uint32_t base_address_raw = allocateFrameBufferTag.GetBaseAddress();
    uint32_t size_in_bytes = allocateFrameBufferTag.GetSizeInBytes();
    uint32_t pitch = getPitchTag.GetPitch();

    LogError("FB DIAG: applied W=%u H=%u ADDR=0x%x SIZE=%u PITCH=%u\n",
             applied_width, applied_height, base_address_raw, size_in_bytes, pitch);

    if ((applied_width == 0) ||
        (applied_height == 0) ||
        (base_address_raw == 0) ||
        (size_in_bytes == 0) ||
        (pitch == 0))
    {
        LogError("FB DIAG: one or more applied values were zero -- returning false\n");
        return false;
    }

    //  See AllocateFrameBufferTag's comment in gpu_mailbox_messages.h --
    //      the GPU has historically returned a "bus address" with the top
    //      bits used as a cache-alias selector rather than a plain ARM
    //      physical address. Masking them off is the standard Pi3/4/5
    //      convention.

    base_address_ = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(base_address_raw & 0x3FFFFFFF));
    size_in_bytes_ = size_in_bytes;
    pitch_ = pitch;
    width_ = applied_width;
    height_ = applied_height;

    LogError("FB DIAG: Allocate() succeeded, base_address_=%p\n", (void *)base_address_);

    return true;
}

void VideoFrameBuffer::ScrollUp(uint32_t rows, uint32_t fill_color)
{
    if (!IsAllocated() || (rows == 0))
    {
        return;
    }

    uint32_t words_per_row = pitch_ / 4;
    uint32_t shift_words = words_per_row * rows;
    uint32_t total_words = words_per_row * height_;

    volatile uint32_t *base = reinterpret_cast<volatile uint32_t *>(base_address_);

    uint32_t i = 0;

    while (i + shift_words < total_words)
    {
        base[i] = base[i + shift_words];
        i++;
    }

    while (i < total_words)
    {
        base[i] = fill_color;
        i++;
    }
}