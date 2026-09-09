// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Deliberate copies of the four sc_* stubs from src/asm/system_calls.S.
//
//  They are duplicated rather than shared because the KERNEL's copy must never be linked
//      into a user image: everything in src/ is built for the kernel's link address and
//      its includes, and a user binary that pulled any of it in would be linking kernel
//      code into EL0.  The ABI is the contract, not the file -- w8 holds the syscall
//      number, x0..x3 the arguments, and svc #0 makes the call.

#include "task/system_calls.h"

    .section .text, "ax"

    .globl sc_Write
sc_Write:
    mov     w8, #SYS_WRITE_NUMBER
    svc     #0
    ret

    .globl sc_Malloc
sc_Malloc:
    mov     w8, #SYS_MALLOC_NUMBER
    svc     #0
    ret

    .globl sc_Exit
sc_Exit:
    mov     w8, #SYS_EXIT_NUMBER
    svc     #0
    ret

    .globl sc_Yield
sc_Yield:
    mov     w8, #SYS_YIELD_NUMBER
    svc     #0
    ret
    