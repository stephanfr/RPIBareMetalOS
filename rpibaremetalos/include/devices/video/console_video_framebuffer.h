// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <synchronization.h>

#include "devices/video/video_framebuffer.h"
#include "devices/character_io.h"

//
//  Text console rendered onto a VideoFrameBuffer using an 8x8 bitmap font.
//      This is also a CharacterIODevice like UART0/UART1, so it can be registered with
//      GetOSEntityRegistry() and used anywhere a CharacterIODevice is
//      expected -- but unlike them, allocation can legitimately fail (no
//      monitor attached, running under QEMU with no display), so callers
//      MUST check IsAllocated() before registering/using this device,
//      rather than treating construction failure as fatal the way
//      SetupSerialConsole() does for UART0/UART1.
//
//  getc() has no real input source -- this device is write-only. It
//      always returns 0 immediately rather than blocking, since nothing
//      produces input for it.

class ConsoleVideoFrameBuffer : public CharacterIODevice, public VideoFrameBuffer 
{
public:
    ConsoleVideoFrameBuffer(const char *alias, uint32_t foreground_color, uint32_t background_color);

    virtual ~ConsoleVideoFrameBuffer() {}

    void putc(unsigned int c) override;
    unsigned int getc(void) override;

private:
    SpinLock lock_;

    static constexpr uint32_t GLYPH_WIDTH = 8;
    static constexpr uint32_t GLYPH_HEIGHT = 8;

    uint32_t foreground_color_;
    uint32_t background_color_;
    uint32_t cursor_column_ = 0;
    uint32_t cursor_row_ = 0;

    uint32_t Columns() const
    {
        return Width() / GLYPH_WIDTH;
    }

    uint32_t Rows() const
    {
        return Height() / GLYPH_HEIGHT;
    }

    void DrawGlyph(unsigned char c);
    void AdvanceCursorAfterGlyph();
};