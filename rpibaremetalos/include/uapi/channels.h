// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Channels, as a driver process sees them.  Part of the user ABI (include/uapi): plain C, also included by the kernel.
//
//  A channel carries requests from the kernel to the driver serving it, one at a time, and the driver's replies back.
//      sc_ChannelOpen(name, &handle, &page, &data_size) makes the caller the channel's driver for as long as it lives,
//      and maps the channel's memory at 'page', read-write and never executable.  A channel has one driver at a time;
//      opening it again from that driver returns the same answers.
//
//  The memory is a control page, holding the request and the reply, then the channel's data area, data_size bytes
//      at page + CHANNEL_DATA_OFFSET, for bulk data in either direction: a block driver's sectors, say.  Some channels
//      have none.
//
//  sc_ChannelReceive(handle, timeout, &data_length) blocks for the next request and returns its length.  The request
//      is at page + CHANNEL_REQUEST_OFFSET, and data_length bytes the kernel sent with it are at the start of the
//      data area; data_length may be 0 (a null pointer) when the driver doesn't need it.  The driver writes its reply
//      at page + CHANNEL_REPLY_OFFSET, and any data for the kernel at the start of the data area, then calls
//      sc_ChannelReply(handle, length, data_length) before it receives again.  A caller that gives up doesn't free the
//      driver from replying: that reply returns CHANNEL_ABANDONED and is dropped.
//
//      sc_ChannelOpen("test-echo", &channel, &page, &data_size);
//      for (;;) { int length = sc_ChannelReceive(channel, CHANNEL_WAIT_FOREVER, &in); ...; sc_ChannelReply(channel, n, out); }

#include <stdint.h>

#define CHANNEL_SUCCESS             0
#define CHANNEL_NOT_FOUND           -1      /* no channel of that name */
#define CHANNEL_NOT_PERMITTED       -2      /* not in the caller's capabilities */
#define CHANNEL_BUSY                -3      /* another process serves it */
#define CHANNEL_BAD_ADDRESS         -4
#define CHANNEL_INVALID_HANDLE      -5      /* not a channel the caller serves */
#define CHANNEL_TIMED_OUT           -6
#define CHANNEL_NO_MEMORY           -7      /* the memory could not be mapped */
#define CHANNEL_TOO_LONG            -8      /* a message or data longer than the channel or the caller takes */
#define CHANNEL_NOT_SERVING         -9      /* a reply with no request received */
#define CHANNEL_REPLY_PENDING       -10     /* a receive before replying to the last request */
#define CHANNEL_ABANDONED           -11     /* the caller gave up before the reply */
#define CHANNEL_NO_SERVER           -12     /* to kernel callers: no driver serves the channel */
#define CHANNEL_SERVER_EXITED       -13     /* to kernel callers: the driver exited holding the request */

#define CHANNEL_NAME_LENGTH         32      /* including the terminating NUL */
#define CHANNEL_WAIT_FOREVER        0       /* sc_ChannelReceive's timeout */

//  The control page.  Requests and replies are messages: headers and status, at most CHANNEL_MESSAGE_MAX bytes.

#define CHANNEL_CONTROL_SIZE        0x1000
#define CHANNEL_MESSAGE_MAX         0x800
#define CHANNEL_REQUEST_OFFSET      0x000
#define CHANNEL_REPLY_OFFSET        0x800

//  The data area follows the control page; a channel's is a whole number of pages, at most CHANNEL_DATA_MAX bytes.

#define CHANNEL_DATA_OFFSET         CHANNEL_CONTROL_SIZE
#define CHANNEL_DATA_MAX            0x3F000

//  For testing the channel calls: `test drivers` sends hello.bin requests on it.  It has a 64 KB data area.

#define CHANNEL_NAME_TEST_ECHO      "test-echo"
