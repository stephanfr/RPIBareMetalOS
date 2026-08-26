// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/video/console_video_framebuffer.h"
#include "devices/video/fonts/basic_8x8.h"

ConsoleVideoFrameBuffer::ConsoleVideoFrameBuffer(const char *alias, uint32_t foreground_color, uint32_t background_color)
    : VideoFrameBuffer(),
      CharacterIODevice(true, "ConsoleVideoFrameBuffer", alias),
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

    const unsigned char *glyph = font8x8_basic[c];

    uint32_t origin_x = cursor_column_ * GLYPH_WIDTH;
    uint32_t origin_y = cursor_row_ * GLYPH_HEIGHT;

    for (uint32_t row = 0; row < GLYPH_HEIGHT; row++)
    {
        unsigned char bits = glyph[row];

        for (uint32_t col = 0; col < GLYPH_WIDTH; col++)
        {
            uint32_t color = (bits & (1u << col)) ? foreground_color_ : background_color_;
            PutPixel(origin_x + col, origin_y + row, color);
        }
    }
}

void ConsoleVideoFrameBuffer::AdvanceCursorAfterGlyph()
{
    if (cursor_row_ >= Rows())
    {
        ScrollUp(GLYPH_HEIGHT, background_color_);
        cursor_row_ = Rows() - 1;
    }
}

void ConsoleVideoFrameBuffer::putc(unsigned int c)
{
    if (!IsAllocated())
    {
        return;
    }

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
    return 0;
}