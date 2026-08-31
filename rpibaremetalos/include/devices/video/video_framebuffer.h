// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "platform/gpu_mailbox.h"
#include "platform/gpu_mailbox_messages.h"


class VideoFrameBuffer
{
public:
    VideoFrameBuffer() = default;
    virtual ~VideoFrameBuffer() = default;

    //  Asks the firmware for whatever physical resolution it already
    //      negotiated with the monitor over EDID, then requests a
    //      matching framebuffer at 32bpp (see SetColourDepthTag's comment
    //      in gpu_mailbox_messages.h for why 32 rather than anything
    //      else). Falls back to a resolution effectively every HDMI
    //      monitor supports if that query fails.
    //
    //  Returns false, leaving the framebuffer unallocated, on any mailbox
    //      failure or a zero/nonsensical response. This is an ordinary,
    //      expected outcome (no monitor attached, running under QEMU with
    //      no display), not treated as fatal -- callers must check
    //      IsAllocated() before using this object.

    bool Allocate();

    bool IsAllocated() const
    {
        return base_address_ != nullptr;
    }

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    uint32_t Pitch() const { return pitch_; }
    uint32_t BitsPerPixel() const { return bytes_per_pixel_ * 8; }

    //  Packs an 0x00RRGGBB triple into the pixel format the framebuffer
    //      was allocated with.

    static uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b)
    {
        return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }

    uint32_t NativePixel(uint32_t color) const
    {
        if (bytes_per_pixel_ == 2)
        {
            uint32_t r = (color >> 16) & 0xFF;
            uint32_t g = (color >> 8) & 0xFF;
            uint32_t b = color & 0xFF;

            return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);   //  RGB565
        }

        return color;
    }

    void WritePixel(volatile uint8_t *row, uint32_t x, uint32_t native_color) const
    {
        if (bytes_per_pixel_ == 2)
        {
            reinterpret_cast<volatile uint16_t *>(row)[x] = static_cast<uint16_t>(native_color);
        }
        else
        {
            reinterpret_cast<volatile uint32_t *>(row)[x] = native_color;
        }
    }

    void PutPixel(uint32_t x, uint32_t y, uint32_t color)
    {
        if (!IsAllocated() || (x >= width_) || (y >= height_))
        {
            return;
        }

        WritePixel(RowAt(y), x, NativePixel(color));
    }

    void FillRect(uint32_t x, uint32_t y, uint32_t rect_width, uint32_t rect_height, uint32_t color)
    {
        if (!IsAllocated())
        {
            return;
        }

        uint32_t x_end = (x + rect_width > width_) ? width_ : (x + rect_width);
        uint32_t y_end = (y + rect_height > height_) ? height_ : (y + rect_height);

        uint32_t native = NativePixel(color);

        for (uint32_t row = y; row < y_end; row++)
        {
            volatile uint8_t *dest_row = RowAt(row);

            for (uint32_t col = x; col < x_end; col++)
            {
                WritePixel(dest_row, col, native);
            }
        }

        FlushRect(x, y, x_end - x, y_end - y);
    }

    void Clear(uint32_t color)
    {
        FillRect(0, 0, width_, height_, color);
    }

protected:

    void ScrollUp(uint32_t rows, uint32_t fill_color);

    void FlushRect(uint32_t x, uint32_t y, uint32_t rect_width, uint32_t rect_height) const
    {
        if (!IsAllocated() || (rect_width == 0) || (rect_height == 0))
        {
            return;
        }

        uint32_t x_end = (x + rect_width > width_) ? width_ : (x + rect_width);
        uint32_t y_end = (y + rect_height > height_) ? height_ : (y + rect_height);

        for (uint32_t row = y; row < y_end; row++)
        {
            uintptr_t start = reinterpret_cast<uintptr_t>(RowAt(row) + (x * bytes_per_pixel_)) & ~static_cast<uintptr_t>(63);
            uintptr_t end = reinterpret_cast<uintptr_t>(RowAt(row) + (x_end * bytes_per_pixel_));
            
            for (uintptr_t addr = start; addr < end; addr += 64)
            {
                asm volatile("dc civac, %0" :: "r"(addr) : "memory");
            }
        }

        asm volatile("dsb sy" ::: "memory");
    }

    volatile uint8_t *RowAt(uint32_t y) const
    {
        return base_address_ + (static_cast<uint64_t>(y) * pitch_);
    }

private:
    static constexpr uint32_t DEPTH_BITS_PER_PIXEL = 32;
    static constexpr uint32_t DEFAULT_FALLBACK_WIDTH = 1024;
    static constexpr uint32_t DEFAULT_FALLBACK_HEIGHT = 768;
    static constexpr uint32_t ALLOCATE_ALIGNMENT_BYTES = 4096;

    volatile uint8_t *base_address_ = nullptr;
    
    uint32_t size_in_bytes_ = 0;
    uint32_t pitch_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t bytes_per_pixel_ = 4;
};