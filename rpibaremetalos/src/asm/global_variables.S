// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpu_part_nums.h"       //  For default value of board type


//  Global variable for holding the hw board type

.global __hw_board_type
.data
.align 4

__hw_board_type : .word RPI_BOARD_ENUM_UNKNOWN
