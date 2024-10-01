// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpu_part_nums.h"       //  For default value of board type

//  Global variable for holding the hw board type

.global __hw_board_type
.data
.align 4

__hw_board_type : .word RPI_BOARD_ENUM_UNKNOWN

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

__core_jump_table : .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0
                    .quad 0

