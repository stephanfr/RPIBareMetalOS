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
    //      monitor supports if that query fails.  The class will adapt
    //      to whatever depth the firmware actually applies, which may be
    //      less than 32bpp (RPi5 returns 16bpp RGB even when 32bpp is requested).
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

    //  The last framebuffer the firmware allocated, physical, or 0 and 0.  EL2 keeps it mapped when it takes the
    //      VideoCore's memory away from the kernel.

    static uint64_t AllocatedPhysicalBase()
    {
        return allocated_physical_base_;
    }

    static uint64_t AllocatedSizeInBytes()
    {
        return allocated_size_in_bytes_;
    }

    //  Double buffering: up to MAX_BUFFERS screens, stacked in one allocation.  Drawing goes to the DrawTo buffer and
    //      the display scans out the Show buffer; both start at 0.  Show changes the virtual offset, which needs no
    //      reallocation, so it works through EL2 once stage-2 is on.
    //
    //      fb.DrawTo(1 - fb.ShownBuffer());
    //      fb.Clear(color);  ...
    //      fb.Show(1 - fb.ShownBuffer());
    //
    //  TODO: NOT THREAD SAFE -- fix before a second user draws.  DrawTo, Show and the drawing calls read and write
    //      draw_buffer_ and shown_buffer_ with no lock.  ConsoleVideoFrameBuffer's lock covers only its own putc, so a
    //      DrawTo from another task while the console prints sends the console's text, or the other task's drawing,
    //      to the wrong buffer, and a Show can flip the screen away from the console.  Until a lock covers drawing
    //      and flipping together, only one owner may call DrawTo or Show.

    static constexpr uint32_t MAX_BUFFERS = 2;

    uint32_t BufferCount() const { return buffer_count_; }
    uint32_t ShownBuffer() const { return shown_buffer_; }

    void DrawTo(uint32_t buffer)
    {
        if (buffer < buffer_count_)
        {
            draw_buffer_ = buffer;
        }
    }

    bool Show(uint32_t buffer);

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    uint32_t Pitch() const { return pitch_; }
    uint32_t BitsPerPixel() const { return bytes_per_pixel_ * 8; }

    //  Packs r/g/b into a canonical 0x00RRGGBB value.
    //      Conversion to the framebuffer's actual pixel format happens in NativePixel() at write time,
    //      since the firmware chooses the depth.

    static uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b)
    {
        return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
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
        return base_address_ + ((static_cast<uint64_t>(draw_buffer_) * height_ + y) * pitch_);
    }

private:
    static constexpr uint32_t REQUESTED_DEPTH_BITS_PER_PIXEL = 32;
    static constexpr uint32_t DEFAULT_FALLBACK_WIDTH = 1024;
    static constexpr uint32_t DEFAULT_FALLBACK_HEIGHT = 768;
    static constexpr uint32_t ALLOCATE_ALIGNMENT_BYTES = 4096;

    volatile uint8_t *base_address_ = nullptr;
    
    uint32_t size_in_bytes_ = 0;
    uint32_t pitch_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t bytes_per_pixel_ = 4;
    uint32_t buffer_count_ = 0;
    uint32_t draw_buffer_ = 0;
    uint32_t shown_buffer_ = 0;

    static uint64_t allocated_physical_base_;
    static uint64_t allocated_size_in_bytes_;

    bool AllocateBuffers(uint32_t requested_width, uint32_t requested_height, uint32_t buffers);
};