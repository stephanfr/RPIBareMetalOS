// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "processor_cores.h"

.global ParkCore
ParkCore:

    //  Mark the core as parked

    mrs     x9, MPIDR_EL1
    //  A53/A72: core index in Aff0 (bits 1:0); A76 (RPi5): core index in Aff1 (bits 15:8)
    ubfx    x10, x9, #8, #8
    and     x9, x9, #3
    orr     x9, x9, x10
    adrp    x10, __core_state
    add     x10, x10, :lo12:__core_state
    mov     w11, #CORE_STATE_PARKED
    str     w11, [x10, x9, lsl #2]

infinite_loop_parking_core:

    wfe
    b       infinite_loop_parking_core
    