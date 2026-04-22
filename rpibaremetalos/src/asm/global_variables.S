// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"
#include "cpu_part_nums.h"       //  For default value of board type

//  Global variable for holding the hw board type

.global __hw_board_type
.data
.balign 64

__hw_board_type : .word RPI_BOARD_ENUM_UNKNOWN

//  Global variable for holding the number of cores available

.global __number_of_cores_available
.data
.balign 64

__number_of_cores_available : .word 0

//  Global variable for holding an array indicating which cores are active
//  4 bytes per core slot (one uint32_t); 16 cores maximum.
//  All active core slots fit within a single 64-byte cache line.

.global __core_state
.data
.balign 64

__core_state : .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0
               .word 0

//  Global variable for holding an array indicating core jump destinations
//  8 bytes per core slot (one uint64_t); 16 cores maximum.

.global __core_jump_table
.data
.balign 64

__core_jump_table : .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0
                    .double 0

//  Global variables the the GPU mailbox data needed to start the kernel

.global __board_version_vc_start_cmd_line_message_mailbox_buffer
.global __board_version
.global __videocore_memory_base
.global __videocore_memory_size_in_bytes
.global __kernel_command_line
.data

//  Below is a fully formed GPU mailbox request for the board revision, VC memory info and the kernel command line

.align 16       //  Align to 16 bytes - as the mailbox only uses the top 28 bits
__board_version_vc_start_cmd_line_message_mailbox_buffer :  .word 60 + S_MAX_KERNEL_COMMAND_LINE_LENGTH     //  Buffer size in bytes
                                                            .word 0                                         //  Response code
                                                            .word 0x00010002                                //  Board revision request code
                                                            .word 4                                         //  Response buffer size in bytes
                                                            .word 0                                         //  Request code
__board_version:                                            .word 0                                         //  Board revision
                                                            .word 0x00010006                                //  Videocore memory request code
                                                            .word 8                                         //  Response buffer size in bytes
                                                            .word 0                                         //  Request code
__videocore_memory_base:                                    .word 0                                         //  Videocore memory start address
__videocore_memory_size_in_bytes:                           .word 0                                         //  Videocore memory size
                                                            .word 0x00050001                                //  Kernel Command Line
                                                            .word S_MAX_KERNEL_COMMAND_LINE_LENGTH          //  Response buffer size in bytes
                                                            .word 0                                         //  Request code
__kernel_command_line:                                      .space S_MAX_KERNEL_COMMAND_LINE_LENGTH, 0      //  Kernel command line buffer
                                                            .word 0                                         //  End Tag - must be zero
                                                            .word 0

