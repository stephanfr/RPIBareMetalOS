// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#include "sysregs.h"
#include "cpu_part_nums.h"

//
//  This function checks the PARTNUM field of the MIDR_EL1 register to determine
//      which version of the RPi is executing the code.  The return value maps to
//      a C enum which will be used in the higher level code.
//


.global IdentifyBoardType

IdentifyBoardType :
    mrs x0, midr_el1
    ldr x1, =MIDR_EL1_PARTNUM_MASK
    and x0, x0, x1
    ldr x1, =MIDR_EL1_PARTNUM_CORTEX_A53
    cmp x0, x1
    b.eq ibt_cortex_a53
    ldr x1, =MIDR_EL1_PARTNUM_CORTEX_A72
    cmp x0, x1
    b.eq ibt_cortex_a72
    ldr x0, =RPI_BOARD_ENUM_UNKNOWN
    ret

ibt_cortex_a53 :
    ldr x0, =RPI_BOARD_ENUM_RPI3
    ret
    
ibt_cortex_a72 :
    ldr x0, =RPI_BOARD_ENUM_RPI4
    ret


