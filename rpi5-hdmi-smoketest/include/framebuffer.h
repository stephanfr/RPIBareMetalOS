// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// A tiny text console rendered directly onto a 32bpp linear framebuffer,
// using the public-domain 8x8 bitmap font in font8x8_basic.h.

#pragma once

#include <stdint.h>

#include "mailbox.h"

typedef struct
{
    volatile uint8_t *base;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t fg_color;
    uint32_t bg_color;
} fb_console_t;

void fb_console_init(fb_console_t *console, const fb_info_t *fb, uint32_t fg_color, uint32_t bg_color);
void fb_console_clear(fb_console_t *console);
void fb_console_putc(fb_console_t *console, char c);
void fb_console_puts(fb_console_t *console, const char *s);
void fb_console_fill(fb_console_t *console, uint32_t color);

//  Packs an 0x00RRGGBB triple into the pixel format the framebuffer was
//      allocated with. NOTE: the firmware's pixel-order tag is not always
//      honored -- if red and blue appear swapped on real hardware, swap the
//      shifts here rather than fighting the SET_PIXEL_ORDER tag.

static inline uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
