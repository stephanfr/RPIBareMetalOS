// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  This function is intended only to be used from the boot assembly stub to collect 
//      boot time information so the MMU can be configured properly.
//
//  This function cannot be called after the MMU is enabled nor should it be used if other
//      cores might be accessing the mailbox simultaneously.  In short - use this only during
//      initial boot before the MMU is enabled.

#include "cpu_part_nums.h"

#define MAILBOX_READ 0x0000B880
#define MAILBOX_WRITE 0x0000B8A0
#define MAILBOX_STATUS 0x0000B898
#define MAILBOX_PROPETY_CHANNEL 0x08

#define MAILBOX_STATUS_EMPTY 0x40000000
#define MAILBOX_STATUS_RESPONSE_SUCCESS 0x80000000

x15_mmiobase .req x15

//  extern "C" void GetBootTimeSettings( uint32_t core_type );

.globl GetBootTimeSettings
.align 4
GetBootTimeSettings:

    adrp x0, __hw_board_type
    add x0, x0, :lo12:__hw_board_type
    
    ldr w0, [x0]

    cmp w0, #RPI_BOARD_ENUM_RPI3
	b.ne not_rpi3

    mov x15_mmiobase, #0x3F000000
    b have_mmiobase

not_rpi3:

    cmp w0, #RPI_BOARD_ENUM_RPI4
	b.ne park_core_on_error

    mov x15_mmiobase, #0xFE000000

have_mmiobase:

    adrp x14, __board_version_vc_start_cmd_line_message_mailbox_buffer
    add x14, x14, :lo12:__board_version_vc_start_cmd_line_message_mailbox_buffer
    mov x13, #0xFFFFFFF0
    and x14, x14, x13
    add x14, x14, #MAILBOX_PROPETY_CHANNEL

    mov x13, #MAILBOX_WRITE
    str w14, [x15_mmiobase, x13]

    mov x13, #MAILBOX_READ
    mov x10, #MAILBOX_STATUS

    adrp x14, __board_version_vc_start_cmd_line_message_mailbox_buffer
    add x14, x14, :lo12:__board_version_vc_start_cmd_line_message_mailbox_buffer

loop_until_message_processed:

    ldr w12, [x15_mmiobase, x10]

    mov x11, #MAILBOX_STATUS_EMPTY
    and w12, w12, w11
    cbnz w12, loop_until_message_processed

    ldr w12, [x15_mmiobase, x13]
    and w12, w12, #0x0F
    cmp w12, #MAILBOX_PROPETY_CHANNEL
    b.ne loop_until_message_processed

    ldr w12, [x14, #0x04]
    mov w11, #MAILBOX_STATUS_RESPONSE_SUCCESS
    cmp w12, w11
    b.ne loop_until_message_processed

    ret

park_core_on_error:

    b ParkCore                                          //  If we don't know the board type, park the core
