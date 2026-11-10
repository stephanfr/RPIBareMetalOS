// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The last step of a live update, entered with x0 = board, x1 = core, x2 = role.
//
//  UPDATE_COMMIT copies this to HYPERVISOR_TRAMPOLINE_ADDRESS and every core in the update runs that copy, because
//      the committer overwrites the running image, stacks included.  So it is position independent, uses no stack
//      and no literal pool, and reaches the state only through its fixed address.  It is the old image's code, so
//      nothing in it has to stay compatible across versions.
//
//  Uses x0-x18 only: x19-x29 still hold the kernel's values, and the new image's install preserves them.  On
//      success it returns to the kernel with x0 = 0; if the new install fails, the core parks.

#include "hypervisor_config.h"
#include "hypervisor_state.h"
#include "hypervisor_update.h"

.macro MOV_ADDRESS reg, address
    movz    \reg, #(((\address) >> 16) & 0xFFFF), lsl #16
    movk    \reg, #((\address) & 0xFFFF)
.endm

.section ".text", "ax", %progbits

.balign 8
.globl HypervisorTrampoline
HypervisorTrampoline:
    mov     x3, x0                              //  board
    mov     x4, x1                              //  core
    mov     x5, x2                              //  role
    MOV_ADDRESS x6, HYPERVISOR_STATE_ADDRESS
    add     x7, x6, #HYP_STATE_UPDATE_ARRIVED

    cmp     x5, #HYP_UPDATE_ROLE_COMMITTER
    b.eq    WaitForJoiners

    //  Joiner: off the old image now, so the committer may overwrite it.

    mov     w8, #2
    str     w8, [x7, x4, lsl #2]
    dsb     sy
    sev

1:  ldr     w8, [x6, #HYP_STATE_UPDATE_COPY_DONE]
    cbnz    w8, SynchronizeInstructions
    wfe
    b       1b

WaitForJoiners:
    ldr     w8, [x6, #HYP_STATE_UPDATE_JOIN_MASK]
    mov     x9, #0                              //  core index

2:  cbz     w8, CopyImage
    tbz     w8, #0, 4f
3:  ldr     w10, [x7, x9, lsl #2]
    cmp     w10, #2
    b.eq    4f
    wfe
    b       3b
4:  lsr     w8, w8, #1
    add     x9, x9, #1
    b       2b

    //  Staging to the load address in 8-byte words, then clean and invalidate the result to the point of coherency.

CopyImage:
    ldr     w8, [x6, #HYP_STATE_UPDATE_STAGED_IMAGE_SIZE]
    add     x8, x8, #7
    and     x8, x8, #~7
    MOV_ADDRESS x9, HYPERVISOR_STAGING_ADDRESS
    MOV_ADDRESS x10, HYPERVISOR_LOAD_ADDRESS
    mov     x11, #0

5:  cmp     x11, x8
    b.hs    6f
    ldr     x12, [x9, x11]
    str     x12, [x10, x11]
    add     x11, x11, #8
    b       5b

6:  mrs     x12, CTR_EL0
    ubfx    x12, x12, #16, #4                   //  DminLine: log2 of the line size in words
    mov     x13, #4
    lsl     x13, x13, x12
    mov     x11, x10
    add     x14, x10, x8

7:  dc      civac, x11
    add     x11, x11, x13
    cmp     x11, x14
    b.lo    7b
    dsb     sy

    mov     w12, #1
    str     w12, [x6, #HYP_STATE_UPDATE_COPY_DONE]
    dsb     sy
    sev

SynchronizeInstructions:
    ic      iallu
    dsb     ish
    isb

    MOV_ADDRESS x10, HYPERVISOR_LOAD_ADDRESS
    ldr     w11, [x10, #HYPERVISOR_HDR_ENTRY_OFFSET]
    add     x11, x10, x11

    cmp     x5, #HYP_UPDATE_ROLE_COMMITTER
    b.ne    InstallSecondary

    mov     x0, x3
    mov     x1, x4
    mov     x2, #HYPERVISOR_INSTALL_LIVE_PRIMARY
    blr     x11
    cbnz    x0, Park

    MOV_ADDRESS x6, HYPERVISOR_STATE_ADDRESS     //  the install clobbered x0-x18
    str     wzr, [x6, #HYP_STATE_UPDATE_STAGED_IMAGE_SIZE]
    mov     w8, #1
    str     w8, [x6, #HYP_STATE_UPDATE_IMPORT_DONE]
    dsb     sy
    sev

    mov     x0, #HVC_SUCCESS
    eret

InstallSecondary:
8:  ldr     w8, [x6, #HYP_STATE_UPDATE_IMPORT_DONE]
    cbnz    w8, 9f
    wfe
    b       8b

9:  str     wzr, [x7, x4, lsl #2]
    dsb     sy

    mov     x0, x3
    mov     x1, x4
    mov     x2, #HYPERVISOR_INSTALL_LIVE_SECONDARY
    blr     x11
    cbnz    x0, Park

    mov     x0, #HVC_SUCCESS
    eret

Park:
    wfe
    b       Park

.globl HypervisorTrampolineEnd
HypervisorTrampolineEnd:
