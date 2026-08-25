// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// HDMI/mailbox smoketest entry point. See README.md in this directory for
// what this is, why it exists standalone from the main RPIBareMetalOS
// image, and what to check if nothing shows up on the monitor.

#include <stdint.h>

#include "framebuffer.h"
#include "mailbox.h"

#if defined(BOARD_RPI4)
#define BOARD_NAME "RPI4"
#elif defined(BOARD_RPI5)
#define BOARD_NAME "RPI5"
#endif

static void spin_delay(uint32_t iterations)
{
    for (volatile uint32_t i = 0; i < iterations; i++)
    {
    }
}

//  Freestanding decimal printer (no libc, so no itoa/printf). Only handles
//      non-negative values, which is all fb_info_t ever contains.

static void console_put_uint(fb_console_t *console, uint32_t value)
{
    char digits[10];
    int count = 0;

    if (value == 0)
    {
        fb_console_putc(console, '0');
        return;
    }

    while (value > 0 && count < (int)sizeof(digits))
    {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count > 0)
    {
        fb_console_putc(console, digits[--count]);
    }
}

//  Draws a small filled square in the top-left corner that flips between
//      two colors on every loop iteration -- proof the core is alive and
//      still driving the framebuffer, independent of anything text-related.

static void draw_heartbeat(fb_console_t *console, int on)
{
    uint32_t color = on ? fb_rgb(0xFF, 0x00, 0x00) : fb_rgb(0x00, 0x00, 0x00);

    for (uint32_t y = 0; y < 16 && y < console->height; y++)
    {
        volatile uint32_t *row = (volatile uint32_t *)(console->base + (uint64_t)y * console->pitch);

        for (uint32_t x = 0; x < 16 && x < console->width; x++)
        {
            row[x] = color;
        }
    }
}

void kernel_main(void)
{
    fb_info_t fb;
    fb_console_t console;

    if (!fb_allocate(&fb))
    {
        //  Nothing we can do without a working console device (no UART is
        //      assumed here -- see README.md). Sit and let the heartbeat-less
        //      hang itself be the signal that mailbox setup failed.

        while (1)
        {
            __asm__ volatile("wfe");
        }
    }

    fb_console_init(&console, &fb, fb_rgb(0x00, 0xFF, 0x00), fb_rgb(0x00, 0x00, 0x00));
    fb_console_clear(&console);

    fb_console_puts(&console, "RPIBareMetalOS HDMI/mailbox smoketest\n");
    fb_console_puts(&console, "Board: " BOARD_NAME "\n\n");

    //  Print what GET_PHYSICAL_WIDTH_HEIGHT reported vs. what the firmware
    //      actually applied to SET_PHYSICAL_WIDTH_HEIGHT/SET_VIRTUAL_WIDTH_HEIGHT
    //      (see the comment in mailbox.c's fb_allocate() -- the response is
    //      not guaranteed to match the request). If the image looks
    //      stretched or the wrong aspect ratio, compare "applied physical"
    //      below against the monitor's actual native resolution: a
    //      mismatch there means the firmware locked the real HDMI output to
    //      a different mode than what it told us it applied, and our
    //      buffer is being scaled non-uniformly to fill it.

    fb_console_puts(&console, "GET_PHYSICAL_WIDTH_HEIGHT reported: ");
    console_put_uint(&console, fb.requested_width);
    fb_console_puts(&console, "x");
    console_put_uint(&console, fb.requested_height);
    fb_console_puts(&console, "\n");

    fb_console_puts(&console, "Applied physical (scanned out):     ");
    console_put_uint(&console, fb.width);
    fb_console_puts(&console, "x");
    console_put_uint(&console, fb.height);
    fb_console_puts(&console, "\n");

    fb_console_puts(&console, "Applied virtual (buffer):           ");
    console_put_uint(&console, fb.virtual_width);
    fb_console_puts(&console, "x");
    console_put_uint(&console, fb.virtual_height);
    fb_console_puts(&console, "\n");

    fb_console_puts(&console, "Pitch: ");
    console_put_uint(&console, fb.pitch);
    fb_console_puts(&console, " bytes/row (");
    console_put_uint(&console, fb.pitch / 4);
    fb_console_puts(&console, " px @ 32bpp)\n\n");

    //  Draw a set of solid color bars across the full width. If this
    //      appears with the right colors in the right order, the pixel
    //      format (BGR vs RGB, depth, pitch) is all correct; if colors are
    //      wrong or the bars are torn/offset, that narrows down whether the
    //      problem is pixel order, pitch, or the base-address mask in
    //      mailbox.c's fb_allocate().

    uint32_t bar_colors[] = {
        fb_rgb(0xFF, 0x00, 0x00),
        fb_rgb(0x00, 0xFF, 0x00),
        fb_rgb(0x00, 0x00, 0xFF),
        fb_rgb(0xFF, 0xFF, 0x00),
        fb_rgb(0xFF, 0xFF, 0xFF),
    };

    uint32_t bar_count = sizeof(bar_colors) / sizeof(bar_colors[0]);
    uint32_t bar_width = console.width / bar_count;

    for (uint32_t bar = 0; bar < bar_count; bar++)
    {
        for (uint32_t y = 64; y < 128 && y < console.height; y++)
        {
            volatile uint32_t *row = (volatile uint32_t *)(console.base + (uint64_t)y * console.pitch);

            for (uint32_t x = bar * bar_width; x < (bar + 1) * bar_width && x < console.width; x++)
            {
                row[x] = bar_colors[bar];
            }
        }
    }

    int heartbeat_on = 0;

    while (1)
    {
        draw_heartbeat(&console, heartbeat_on);
        heartbeat_on = !heartbeat_on;
        spin_delay(20000000);
    }
}
