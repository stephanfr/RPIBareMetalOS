// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The console channel, between the kernel and the console UART driver.  Part of the user ABI (include/uapi).
//
//  Output: the channel's data area holds a ring of bytes for the driver to transmit.  The kernel writes bytes, then
//      advances the head; the driver transmits them, then advances the tail.  Both are free-running uint32_t counts,
//      so head - tail is the number waiting, and a byte's place is its count modulo CONSOLE_TX_RING_SIZE.  Each side
//      writes only its own index, with release ordering, and reads the other's with acquire.  The kernel signals the
//      channel when it adds bytes.  The driver turns "\n" into "\r\n".
//
//  Input: the driver posts what it receives (sc_ChannelPost): a ConsoleInputHeader, then the bytes.
//
//  There are no requests: the driver replies to any with no data.

#include <stdint.h>

#include "channels.h"

#define CHANNEL_NAME_CONSOLE        "console"

//  Offsets in the data area.

#define CONSOLE_TX_HEAD_OFFSET      0x0000
#define CONSOLE_TX_TAIL_OFFSET      0x0040          /* a cache line from the head */
#define CONSOLE_TX_RING_OFFSET      0x1000
#define CONSOLE_TX_RING_SIZE        0x4000          /* a power of two */

#define CONSOLE_DATA_SIZE           (CONSOLE_TX_RING_OFFSET + CONSOLE_TX_RING_SIZE)

typedef struct ConsoleInputHeader
{
    uint32_t interrupts;            //  receive interrupts since the last post
    uint32_t errors;                //  bytes received with a framing, parity, break or overrun error
} ConsoleInputHeader;

#define CONSOLE_INPUT_MAX_BYTES     (CHANNEL_POST_MAX - sizeof(ConsoleInputHeader))
