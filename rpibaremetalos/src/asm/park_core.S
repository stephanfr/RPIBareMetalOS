// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "core_states.h"

.global ParkCore
ParkCore:

    //  Mark the core as parked

    mrs     x0, MPIDR_EL1
    and     x0, x0, #3
    adrp    x1, __core_state
    add     x1, x1, :lo12:__core_state
    mov     w2, #CORE_STATE_PARKED
    str     w2, [x1, x0, lsl #2]

.infinite_loop_parking_core:

    wfe
    b       .infinite_loop_parking_core
    