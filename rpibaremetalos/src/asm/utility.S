// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

.global CPUTicksDelay
CPUTicksDelay:
    subs x0, x0, #1
    bne CPUTicksDelay
    ret
	
.global put32
put32:
	str w1,[x0]
	ret

.global get32
get32:
	ldr w0,[x0]
	ret

//	Returns the current value of the stack pointer
//
//	Arguments:
//
//	Returns:
//		x0 : pointer to the bottom of the stack

.global get_stack_pointer
get_stack_pointer:
	mov x0, sp
	ret

	