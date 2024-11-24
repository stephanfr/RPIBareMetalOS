// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"
#include "cpu_part_nums.h"       //  For default value of board type

//  Global variable for holding the hw board type

.global __hw_board_type
.data
.align 4

__hw_board_type : .word RPI_BOARD_ENUM_UNKNOWN

//  Global variable for holding the bottom of videocore memory

.global __videocore_memory_base
.data
.align 4

__videocore_memory_base : .double

//  Global variable for holding the number of cores available

.global __number_of_cores_available
.data
.align 4

__number_of_cores_available : .word 0

//  Global variable for holding and array indicating which cores are active

.global __core_state
.data
.align 4

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

//  Global variable for holding and array indicating which cores are active

.global __core_jump_table
.data
.align 4

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
.global __vc_memory_start
.global __vc_memory_size
.global __kernel_command_line
.data
.align 16       //  Align to 16 bytes - as the mailbox only uses the top 28 bits

__board_version_vc_start_cmd_line_message_mailbox_buffer :  .word 60 + MAX_KERNEL_COMMAND_LINE_LENGTH       //  Buffer size in bytes
                                                            .word 0                                         //  Response code
                                                            .word 0x00010002                                //  Board revision request code
                                                            .word 4                                         //  Response buffer size in bytes
                                                            .word 0                                         //  Request code
__board_version:                                            .word 0                                         //  Board revision
                                                            .word 0x00010006                                //  Videocore memory request code
                                                            .word 8                                         //  Response buffer size in bytes
                                                            .word 0                                         //  Request code
__vc_memory_start:                                          .word 0                                         //  Videocore memory start address
__vc_memory_size:                                           .word 0                                         //  Videocore memory size
                                                            .word 0x00050001                                //  Kernel Command Line
                                                            .word MAX_KERNEL_COMMAND_LINE_LENGTH            //  Response buffer size in bytes
                                                            .word 0                                         //  Request code
__kernel_command_line:                                      .space MAX_KERNEL_COMMAND_LINE_LENGTH, 0        //  Kernel command line buffer
                                                            .word 0                                         //  End Tag - must be zero
                                                            .word 0

