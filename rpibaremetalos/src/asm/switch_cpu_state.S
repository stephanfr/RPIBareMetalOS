// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//
//	This method assumes there are pointers to Task::CPUState instance in x0 and x1.
//		The current state gets saved in the instance pointed to by x0 and the state pointed
//		to by x1 gets restored to the CPU.
//
//	Not everything in the CPU is saved, just a subset of registers.  These registers are
//		are guaranteed to be unmodified following a function call.  Since task switching occurs
//		as a result of a call to 'schedule', the other registers are assumed by the compiler to
//		be volatile.
//

.globl SwitchCPUState
SwitchCPUState:
	mov	x9, sp
	stp	x19, x20, [x0], #16
	stp	x21, x22, [x0], #16
	stp	x23, x24, [x0], #16
	stp	x25, x26, [x0], #16
	stp	x27, x28, [x0], #16
	stp	x29, x9, [x0], #16
	str	x30, [x0], #8
	mrs x2, tpidrro_el0
	mrs x3, tpidr_el1
	stp x2, x3, [x0], #16

	ldp	x19, x20, [x1], #16
	ldp	x21, x22, [x1], #16
	ldp	x23, x24, [x1], #16
	ldp	x25, x26, [x1], #16
	ldp	x27, x28, [x1], #16
	ldp	x29, x9, [x1], #16
	ldr	x30, [x1], #8
	ldp x2, x3, [x1], #16
	msr tpidrro_el0, x2
	msr tpidr_el1, x3
	mov	sp, x9
	ret
