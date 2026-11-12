// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/video/video_framebuffer.h"
#include "platform/address_space_layout.h"

uint64_t VideoFrameBuffer::allocated_physical_base_ = 0;
uint64_t VideoFrameBuffer::allocated_size_in_bytes_ = 0;

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

    //  Two buffers when the VideoCore has room for them, else one.

    return AllocateBuffers(requested_width, requested_height, MAX_BUFFERS) ||
           AllocateBuffers(requested_width, requested_height, 1);
}

bool VideoFrameBuffer::AllocateBuffers(uint32_t requested_width, uint32_t requested_height, uint32_t buffers)
{
    GPUMailbox mbox;

    //  One combined message: set physical/virtual size, offset, depth,
    //      pixel order, allocate the buffer, and read back the pitch.
    //      The buffers are stacked vertically in the virtual screen.

    SetPhysicalWidthHeightTag setPhysicalWidthHeightTag(requested_width, requested_height);
    SetVirtualWidthHeightTag setVirtualWidthHeightTag(requested_width, requested_height * buffers);
    SetVirtualOffsetTag setVirtualOffsetTag(0, 0);
    SetColourDepthTag setColourDepthTag(REQUESTED_DEPTH_BITS_PER_PIXEL);
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
    uint32_t applied_virtual_height = setVirtualWidthHeightTag.GetAppliedHeight();
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

    //  The firmware may trim the virtual height; anything short of every buffer is a failure, so the caller can ask
    //      for fewer.

    if ((applied_virtual_height < applied_height * buffers) ||
        (static_cast<uint64_t>(size_in_bytes) < static_cast<uint64_t>(pitch) * applied_height * buffers))
    {
        return false;
    }

    //  The depth tag's own response and the pitch should agree; the pitch wins
    //      if they don't, since it describes the buffer that was really allocated.

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

    allocated_physical_base_ = base_address_raw & 0x3FFFFFFF;
    allocated_size_in_bytes_ = size_in_bytes;

    base_address_ = reinterpret_cast<volatile uint8_t *>(PhysicalToKernelVirtualAddress(static_cast<uintptr_t>(allocated_physical_base_)));
    size_in_bytes_ = size_in_bytes;
    pitch_ = pitch;
    width_ = applied_width;
    height_ = applied_height;
    buffer_count_ = buffers;
    draw_buffer_ = 0;
    shown_buffer_ = 0;

    return true;
}

bool VideoFrameBuffer::Show(uint32_t buffer)
{
    if (!IsAllocated() || (buffer >= buffer_count_))
    {
        return false;
    }

    SetVirtualOffsetTag setVirtualOffsetTag(0, buffer * height_);
    GPUMailboxPropertyMessage message(setVirtualOffsetTag);

    if (!GPUMailbox().sendMessage(message) || (setVirtualOffsetTag.GetAppliedYOffset() != buffer * height_))
    {
        return false;
    }

    shown_buffer_ = buffer;

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

    volatile uint32_t *base = reinterpret_cast<volatile uint32_t *>(RowAt(0));

    //  The copy below moves whole 32-bit words regardless of depth, so the fill
    //      value must be the native pixel replicated across the word -- at 16bpp
    //      one word covers two pixels.

    uint32_t i = 0;

    while (i + shift_words < total_words)
    {
        base[i] = base[i + shift_words];
        i++;
    }

    uint32_t native = NativePixel(fill_color);
    uint32_t fill_word = (bytes_per_pixel_ == 2) ? ((native & 0xFFFF) | (native << 16)) : native;
    
    while (i < total_words)
    {
        base[i] = fill_word;
        i++;
    }

    FlushRect(0, 0, width_, height_);
}
