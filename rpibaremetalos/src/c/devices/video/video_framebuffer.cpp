// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/video/video_framebuffer.h"

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

    if (query_ok &&
        (getPhysicalWidthHeightTag.GetWidth() != 0) &&
        (getPhysicalWidthHeightTag.GetHeight() != 0))
    {
        requested_width = getPhysicalWidthHeightTag.GetWidth();
        requested_height = getPhysicalWidthHeightTag.GetHeight();
    }

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
    uint32_t applied_depth = setColourDepthTag.GetAppliedBitsPerPixel();
    uint32_t base_address_raw = allocateFrameBufferTag.GetBaseAddress();
    uint32_t size_in_bytes = allocateFrameBufferTag.GetSizeInBytes();
    uint32_t pitch = getPitchTag.GetPitch();

    if ((applied_width == 0) ||
        (applied_height == 0) ||
        (base_address_raw == 0) ||
        (size_in_bytes == 0) ||
        (pitch == 0))
    {
        return false;
    }

    //  The firmware does not always honour the requested depth -- RPi5 returns
    //      16bpp RGB565 even when 32bpp is asked for. Derive the real value from
    //      the pitch (ground truth for the buffer that was actually allocated)
    //      rather than trusting SetColourDepthTag's response, and let the
    //      renderers below adapt instead of painting 32-bit words into 16-bit
    //      pixels -- which lands one word across two pixels and turns green into
    //      gold with every second pixel black.

    uint32_t bytes_per_pixel = pitch / applied_width;

    if ((bytes_per_pixel != 2) && (bytes_per_pixel != 4))
    {
        return false;
    }

    bytes_per_pixel_ = bytes_per_pixel;

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

    //  The copy below moves whole 32-bit words regardless of depth, so the fill
    //      value must be the native pixel replicated across the word -- at 16bpp
    //      one word covers two pixels.

    uint32_t native = NativePixel(fill_color);
    uint32_t fill_word = (bytes_per_pixel_ == 2) ? ((native & 0xFFFF) | (native << 16)) : native;
    
    while (i < total_words)
    {
        base[i] = fill_word;
        i++;
    }

    FlushRect(0, 0, width_, height_);
}
