// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/video/console_video_framebuffer.h"
#include "devices/video/fonts/basic_8x8.h"

ConsoleVideoFrameBuffer::ConsoleVideoFrameBuffer(const char *alias, uint32_t foreground_color, uint32_t background_color)
    : CharacterIODevice(true, "ConsoleVideoFrameBuffer", alias),
      VideoFrameBuffer(),
      foreground_color_(foreground_color),
      background_color_(background_color)
{
    if (Allocate())
    {
        Clear(background_color_);
    }

    //  NOTE: if Allocate() failed here, this object is constructed but
    //      IsAllocated() is false -- putc() below is a no-op in that
    //      state. Callers must check IsAllocated() before registering
    //      this device with GetOSEntityRegistry(); this constructor does
    //      not park the core or throw the way a hard dependency would.
}

void ConsoleVideoFrameBuffer::DrawGlyph(unsigned char c)
{
    if (c >= 128)
    {
        c = '?';
    }

    //  PutPixel()/FillRect() are bypassed below for speed, so their bounds
    //      checks are done once here instead. putc() keeps the cursor inside
    //      Columns()/Rows(), so this is belt-and-braces rather than expected.

    if (!IsAllocated() || (cursor_column_ >= Columns()) || (cursor_row_ >= Rows()))
    {
        return;
    }

    const unsigned char *glyph = font8x8_basic[c];

    uint32_t origin_x = cursor_column_ * CELL_WIDTH;
    uint32_t origin_y = cursor_row_ * CELL_HEIGHT;

    //  Paint the whole cell, then flush it once. FillRect() flushes on every
    //      call and each flush ends in a full 'dsb sy', so calling it per
    //      scaled pixel cost 64 system barriers per character.

    uint32_t foreground = NativePixel(foreground_color_);
    uint32_t background = NativePixel(background_color_);

    for (uint32_t row = 0; row < GLYPH_HEIGHT; row++)
    {
        unsigned char bits = glyph[row];

        for (uint32_t scale_y = 0; scale_y < GLYPH_SCALE; scale_y++)
        {
            volatile uint8_t *dest_row = RowAt(origin_y + (row * GLYPH_SCALE) + scale_y);

            for (uint32_t col = 0; col < GLYPH_WIDTH; col++)
            {
                uint32_t native = (bits & (1u << col)) ? foreground : background;

                for (uint32_t scale_x = 0; scale_x < GLYPH_SCALE; scale_x++)
                {
                    WritePixel(dest_row, origin_x + (col * GLYPH_SCALE) + scale_x, native);
                }
            }
        }
    }

    FlushRect(origin_x, origin_y, CELL_WIDTH, CELL_HEIGHT);
}

void ConsoleVideoFrameBuffer::AdvanceCursorAfterGlyph()
{
    if (cursor_row_ >= Rows())
    {
        ScrollUp(CELL_HEIGHT, background_color_);
        cursor_row_ = Rows() - 1;
    }
}

void ConsoleVideoFrameBuffer::putc(unsigned int c)
{
    if (!IsAllocated())
    {
        return;
    }

    InterruptLockGuard guard(lock_);

    if (c == '\n')
    {
        cursor_column_ = 0;
        cursor_row_++;
    }
    else if (c == '\r')
    {
        cursor_column_ = 0;
    }
    else
    {
        DrawGlyph(static_cast<unsigned char>(c));

        cursor_column_++;

        if (cursor_column_ >= Columns())
        {
            cursor_column_ = 0;
            cursor_row_++;
        }
    }

    AdvanceCursorAfterGlyph();
}

unsigned int ConsoleVideoFrameBuffer::getc(void)
{
    for (;;)
    {
        asm volatile("wfe");
    }
}