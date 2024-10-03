// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

.global CPUTicksDelay
CPUTicksDelay:
    subs x0, x0, #1
    bne CPUTicksDelay
    ret

//	Returns the current value of the stack pointer
//
//	Arguments:
//
//	Returns:
//		x0 : pointer to the bottom of the stack

.global GetStackPointer
GetStackPointer:
	mov x0, sp
	ret


.global GetExceptionLevel
GetExceptionLevel:
    mrs x0, CurrentEL
    lsr x0, x0, #2
    ret


.global SetKernelTaskContext
SetKernelTaskContext:
    msr tpidr_el1, x0
    mov x0, #0
    msr tpidrro_el0, x0
    ret

//
//  Only one of the two thread poitner registers will have a value, the other will have zero.
//      We will return the register with a value.
//

.global GetTaskContext
GetTaskContext:
    mrs x0, tpidrro_el0
    cbnz x0, .have_task_context
    mrs x0, tpidr_el1
.have_task_context:
    ret
