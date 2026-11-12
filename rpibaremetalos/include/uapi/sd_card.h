// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The sd-card channel: the kernel's block device calls the SD card driver for 512-byte blocks.  Part of the user ABI
//      (include/uapi): plain C, also included by the kernel.
//
//  A request is an SDCardRequest.  A write's blocks come with it in the data area; a read's go back there, with the
//      reply.  The reply is an SDCardReply.  The driver serves the channel only once the card is ready, so a served
//      channel is a working card.
//
//  The driver's argument names the controller and its base clock, which only the kernel can ask the firmware for.

#include <stdint.h>

#include "channels.h"

#define CHANNEL_NAME_SD_CARD            "sd-card"

#define SD_CARD_BLOCK_SIZE              512
#define SD_CARD_DATA_SIZE               0x10000
#define SD_CARD_MAX_BLOCKS              (SD_CARD_DATA_SIZE / SD_CARD_BLOCK_SIZE)

#define SD_CARD_READ                    1
#define SD_CARD_WRITE                   2

typedef struct SDCardRequest
{
    uint32_t operation;
    uint32_t block;
    uint32_t count;                     /* 1 to SD_CARD_MAX_BLOCKS */
} SDCardRequest;

#define SD_CARD_SUCCESS                 0
#define SD_CARD_BAD_REQUEST             -1
#define SD_CARD_IO_FAILED               -2      /* the card failed the command after its retries */

//  The counts are the driver's since its last reply, its start included.

typedef struct SDCardReply
{
    int32_t result;
    uint32_t blocks;
    uint32_t command_retries;
    uint32_t transfer_aborts;
    uint32_t card_resets;
} SDCardReply;

//  The controller families, which differ in bring-up.  BCM2837 is the Pi 3's Arasan controller, which comes up
//      powered; BCM2712 is the Pi 5's, with its SDIO_CFG block at 0x400 and a reset that leaves the clock alone.

#define SD_CARD_CONTROLLER_BCM2837      0
#define SD_CARD_CONTROLLER_BCM2711      1
#define SD_CARD_CONTROLLER_BCM2712      2

#define SD_CARD_ARGUMENT(controller, clock_hz)  ((((uint64_t)(controller)) << 32) | (uint32_t)(clock_hz))
#define SD_CARD_ARGUMENT_CONTROLLER(argument)   ((uint32_t)((argument) >> 32))
#define SD_CARD_ARGUMENT_CLOCK_HZ(argument)     ((uint32_t)(argument))
