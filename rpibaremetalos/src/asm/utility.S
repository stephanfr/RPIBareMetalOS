// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


.global GetCoreID
GetCoreID:
    mrs     x0, MPIDR_EL1
    and     x0, x0, #3
    ret


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
    msr tpidrro_el0, x0
    ret

//
//  The same thread context value is placed in both the EL0 and EL1 thread context registers.
//      The ELO register is read-only in EL0.
//

.global GetTaskContext
GetTaskContext:
    mrs x0, tpidrro_el0
    ret
