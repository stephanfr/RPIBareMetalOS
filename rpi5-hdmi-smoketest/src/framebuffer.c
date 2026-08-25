// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "framebuffer.h"
#include "font8x8_basic.h"

#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 8

static uint32_t console_columns(const fb_console_t *console)
{
    return console->width / GLYPH_WIDTH;
}

static uint32_t console_rows(const fb_console_t *console)
{
    return console->height / GLYPH_HEIGHT;
}

static volatile uint32_t *pixel_row(const fb_console_t *console, uint32_t y)
{
    return (volatile uint32_t *)(console->base + (uint64_t)y * console->pitch);
}

void fb_console_init(fb_console_t *console, const fb_info_t *fb, uint32_t fg_color, uint32_t bg_color)
{
    console->base = (volatile uint8_t *)(uintptr_t)fb->framebuffer_addr;
    console->pitch = fb->pitch;
    console->width = fb->width;
    console->height = fb->height;
    console->cursor_col = 0;
    console->cursor_row = 0;
    console->fg_color = fg_color;
    console->bg_color = bg_color;
}

void fb_console_fill(fb_console_t *console, uint32_t color)
{
    for (uint32_t y = 0; y < console->height; y++)
    {
        volatile uint32_t *row = pixel_row(console, y);

        for (uint32_t x = 0; x < console->width; x++)
        {
            row[x] = color;
        }
    }
}

void fb_console_clear(fb_console_t *console)
{
    fb_console_fill(console, console->bg_color);

    console->cursor_col = 0;
    console->cursor_row = 0;
}

//  Shifts the whole framebuffer up by one glyph row (8 scanlines) and clears
//      the newly exposed row at the bottom. Implemented as a plain word copy
//      since there is no libc (and so no memmove) in this freestanding build.

static void scroll_one_row(fb_console_t *console)
{
    uint32_t words_per_row = console->pitch / 4;
    uint32_t glyph_row_words = words_per_row * GLYPH_HEIGHT;
    uint32_t total_words = words_per_row * console->height;

    volatile uint32_t *base = (volatile uint32_t *)console->base;

    uint32_t i = 0;

    while (i + glyph_row_words < total_words)
    {
        base[i] = base[i + glyph_row_words];
        i++;
    }

    while (i < total_words)
    {
        base[i] = console->bg_color;
        i++;
    }
}

static void draw_glyph(fb_console_t *console, unsigned char ch)
{
    if (ch >= 128)
    {
        ch = '?';
    }

    const unsigned char *glyph = font8x8_basic[ch];

    uint32_t origin_x = console->cursor_col * GLYPH_WIDTH;
    uint32_t origin_y = console->cursor_row * GLYPH_HEIGHT;

    for (uint32_t row = 0; row < GLYPH_HEIGHT; row++)
    {
        volatile uint32_t *dest_row = pixel_row(console, origin_y + row);
        unsigned char bits = glyph[row];

        for (uint32_t col = 0; col < GLYPH_WIDTH; col++)
        {
            dest_row[origin_x + col] = (bits & (1u << col)) ? console->fg_color : console->bg_color;
        }
    }
}

static void advance_cursor(fb_console_t *console)
{
    if (console->cursor_row >= console_rows(console))
    {
        scroll_one_row(console);
        console->cursor_row = console_rows(console) - 1;
    }
}

void fb_console_putc(fb_console_t *console, char c)
{
    if (c == '\n')
    {
        console->cursor_col = 0;
        console->cursor_row++;
    }
    else if (c == '\r')
    {
        console->cursor_col = 0;
    }
    else
    {
        draw_glyph(console, (unsigned char)c);

        console->cursor_col++;

        if (console->cursor_col >= console_columns(console))
        {
            console->cursor_col = 0;
            console->cursor_row++;
        }
    }

    advance_cursor(console);
}

void fb_console_puts(fb_console_t *console, const char *s)
{
    while (*s != '\0')
    {
        fb_console_putc(console, *s);
        s++;
    }
}
