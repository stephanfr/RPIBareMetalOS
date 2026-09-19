// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "supervisor_config.h"

.section ".text.boot", "ax", %progbits

.globl _start
_start:

	//  Every core arrives here at EL3 with the MMU off and SP undefined.

	//  Core index.  Same idiom as park_core.S:12-16 - Aff0 is the index on A53/A72 and is
	//      always 0 on the A76, where Aff1 holds it, so OR-ing the two works on every board
	//      without needing to identify it first.
	mrs     x9, MPIDR_EL1
	ubfx    x10, x9, #8, #8
	and     x9, x9, #3
	orr     x9, x9, x10                     // x9 = core index

	//  Per-core EL3 stack.  Must not borrow __per_core_initialization_stack_* from the
	//      kernel: those are reused once boot completes.
	ldr     x10, =__stub_stacks_top
	mov     x11, #MONITOR_STACK_SIZE
	mul     x12, x11, x9
	sub     x10, x10, x12
	mov     sp, x10                         // SP_EL3 (SPSel is 1 at reset)

	//  Exception vectors.  Until this lands, any SMC or fault at EL3 dies silently.
	ldr     x10, =__monitor_vectors
	msr     VBAR_EL3, x10

	//  Enable FP/SIMD, then SCR_EL3 with SMD CLEAR on every board so SMC from a lower EL
	//      traps here rather than raising Undefined.
	msr     CPTR_EL3, xzr
	ldr     x10, =SCR_EL3_VAL
	msr     SCR_EL3, x10
	isb

	//  Hand off to the kernel exactly as before.
	ldr     w4, kernel_entry32
	ldr     w0, dtb_ptr32

	mov     x1, #0
	mov     x2, #0
	mov     x3, #0
	br      x4

.ltorg

.org 0xf0
.globl stub_magic
stub_magic:
	.word 0x5afe570b

.org 0xf4
.globl stub_version
stub_version:
	.word 0

.org 0xf8
.globl dtb_ptr32
dtb_ptr32:
	.word 0x0

.org 0xfc
.globl kernel_entry32
kernel_entry32:
	.word 0x80000
