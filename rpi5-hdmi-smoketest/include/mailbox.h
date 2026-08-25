// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Minimal VideoCore mailbox property-channel client.
//
// Tag numbers below match RPIBareMetalOS's existing MailboxTags enum
// (rpibaremetalos/include/platform/gpu_mailbox_messages.h) so this code can
// be folded back into that framework later without renumbering anything.

#pragma once

#include <stdint.h>

#define MBOX_CH_PROP 8

typedef struct
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch;            // bytes per scanline
    uint32_t depth;             // bits per pixel (always 32 here)
    uint32_t framebuffer_addr;  // ARM-side physical address of the framebuffer
    uint32_t framebuffer_size;  // bytes
} fb_info_t;

// Sends the already-populated message in 'mbox' (a 16-byte-aligned buffer
// of 32-bit words, mbox[0] = total size in bytes, mbox[1] = 0) on the given
// mailbox channel and blocks for the firmware's response.
// Returns 1 if the firmware reported success, 0 on timeout or error.
int mbox_call(volatile uint32_t *mbox, uint8_t channel);

// Asks the firmware for a framebuffer at whatever physical resolution it
// already negotiated with the monitor over EDID during boot (falling back
// to 1024x768 if that query fails), at 32 bits per pixel.
// Returns 1 on success and fills *out; returns 0 on failure.
int fb_allocate(fb_info_t *out);
