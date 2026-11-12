// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  HypervisorInstall(x0 = board, x1 = core, x2 = mode) -> x0 = 0 on success.
//
//  Called at EL2 with the MMU off, SP_EL2 undefined and DAIF masked, from start.S on every core and every entry.
//      Preserves x19-x29.  Leaves SP_EL2 on this core's own stack and VBAR_EL2 on the hypervisor's vectors.

#include "hypervisor_config.h"

.section ".text", "ax", %progbits

.globl HypervisorInstall
HypervisorInstall:
    cmp     x1, #MAX_CORES
    b.hs    1f

    msr     SPSel, #1
    adrp    x9, __hyp_stacks_top
    add     x9, x9, :lo12:__hyp_stacks_top
    mov     x10, #HYPERVISOR_STACK_SIZE
    msub    x9, x10, x1, x9                     //  top - size * core
    mov     sp, x9

    stp     x29, x30, [sp, #-16]!

    adrp    x9, __hyp_vectors
    add     x9, x9, :lo12:__hyp_vectors
    msr     VBAR_EL2, x9
    isb

    bl      HypervisorInstallCore

    ldp     x29, x30, [sp], #16
    ret

1:
    mov     x0, #1
    ret
