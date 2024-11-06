// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  This function is intended only to be used from the boot assembly stub to permit the
//      memory manager to determine the start of the VideoCore memory and setup the page
//      tables accordingly.
//
//  This function cannot be called after the MMU is enabled notr should it be used if other
//      cores might be accessing the mailbox simultaneously.  In short - use this only during
//      initial boot before the MMU is enabled.

#include "cpu_part_nums.h"


#define MAILBOX_READ 0x0000B880
#define MAILBOX_WRITE 0x0000B8A0
#define MAILBOX_STATUS 0x0000B898
#define MAILBOX_PROPETY_CHANNEL 0x08

#define MAILBOX_STATUS_EMPTY 0x40000000
#define MAILBOX_STATUS_RESPONSE_SUCCESS 0x80000000

//  Global variable for holding and array indicating which cores are active

.global __vc_message_mailbox_buffer
.data
.align 16                                                   //  Align to 16 bytes - as the mailbox only uses the top 28 bits

__vc_message_mailbox_buffer :   .word 32                    //  Buffer size in bytes
                                .word 0                     //  Response code
                                .word 0x00010006            //  Videocore memory request code
                                .word 8                     //  Response buffer size in bytes
                                .word 0
                                .word 0                     //  Videocore memory start address
                                .word 0                     //  Videocore memory size
                                .word 0                     //  End Tag - must be zero
                                .word 0
x15_mmiobase .req x15


.globl GetVideocoreMemoryStart;
GetVideocoreMemoryStart:

    cmp x0, #RPI_BOARD_ENUM_RPI3
	b.ne not_rpi3

    mov x15_mmiobase, #0x3F000000
    b have_mmiobase

not_rpi3:

    cmp x0, #RPI_BOARD_ENUM_RPI4
	b.ne park_core_on_error

    mov x15_mmiobase, #0xFE000000

have_mmiobase:

    adrp x14, __vc_message_mailbox_buffer
    add x14, x14, :lo12:__vc_message_mailbox_buffer
    mov x13, #0xFFFFFFF0
    and x14, x14, x13
    add x14, x14, #MAILBOX_PROPETY_CHANNEL

    mov x13, #MAILBOX_WRITE
    str w14, [x15_mmiobase, x13]

    mov x13, #MAILBOX_READ
    mov x10, #MAILBOX_STATUS

    adrp x14, __vc_message_mailbox_buffer
    add x14, x14, :lo12:__vc_message_mailbox_buffer

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

    ldr w0, [x14, #0x14]

    ret

park_core_on_error:

    b ParkCore                                          //  If we don't know the board type, park the core
