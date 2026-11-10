// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  VBAR_EL2 table.  Only a synchronous exception from AArch64 EL1 is expected (an hvc): it is dispatched and returns
//      with every register restored from the frame, which the handler edits to return results.  Every other entry
//      reports a fatal error and parks the core.  HCR_EL2.IMO/FMO/AMO are clear, so interrupts stay at EL1.
//
//  The frame is a HypervisorFrame (hypervisor_hvc.h): x0-x30, then padding, on SP_EL2.

#include "hypervisor_config.h"
#include "hypervisor_update.h"

#define FRAME_SIZE (32 * 8)

//  x0 and x1 are already saved: an entry stub has only 32 instructions.

.macro SAVE_X2_TO_X30
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
.endm

.macro FATAL_VECTOR index
    .balign 0x80
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    mov     x1, #\index
    b       HypervisorFatalEntry
.endm

.macro SYNC_LOWER_VECTOR
    .balign 0x80
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    b       HypervisorSyncLowerEntry
.endm

.section ".vectors", "ax", %progbits

.balign 2048
.globl __hyp_vectors
__hyp_vectors:
    FATAL_VECTOR 0      //  current EL, SP_EL0: sync, IRQ, FIQ, SError
    FATAL_VECTOR 1
    FATAL_VECTOR 2
    FATAL_VECTOR 3
    FATAL_VECTOR 4      //  current EL, SP_EL2
    FATAL_VECTOR 5
    FATAL_VECTOR 6
    FATAL_VECTOR 7
    SYNC_LOWER_VECTOR   //  lower EL, AArch64: sync (8), IRQ, FIQ, SError
    FATAL_VECTOR 9
    FATAL_VECTOR 10
    FATAL_VECTOR 11
    FATAL_VECTOR 12     //  lower EL, AArch32
    FATAL_VECTOR 13
    FATAL_VECTOR 14
    FATAL_VECTOR 15

.section ".text", "ax", %progbits

//  x1 holds the vector index.

HypervisorFatalEntry:
    SAVE_X2_TO_X30

    mov     x0, sp
    bl      HypervisorFatal

1:
    wfe
    b       1b

//  Run to completion: nothing stays on SP_EL2 once this erets or enters the trampoline.

HypervisorSyncLowerEntry:
    SAVE_X2_TO_X30

    mov     x0, sp
    bl      HypervisorHandleSync
    cbnz    x0, ExitToTrampoline

    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #FRAME_SIZE
    eret

//  Live update: only x19-x29 survive (the update HVCs clobber x0-x18 and x30), and nothing here touches SP_EL2's
//      stack again, so the new image can overwrite it.

ExitToTrampoline:
    mov     x9, x0
    ldr     x19, [sp, #152]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]

    ubfx    x0, x9, #HYP_EXIT_BOARD_SHIFT, #8
    ubfx    x1, x9, #HYP_EXIT_CORE_SHIFT, #8
    and     x2, x9, #HYP_EXIT_ROLE_MASK

    isb
    ldr     x10, =HYPERVISOR_TRAMPOLINE_ADDRESS
    br      x10
