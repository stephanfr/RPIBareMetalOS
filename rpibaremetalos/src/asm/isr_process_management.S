// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//	This assembly code borrows pretty heavily from Sergey Matyukevich's code in his rapsberry-pi-os github repository.
//		I am including his license below.  Without his code as a guide, it would have taken me much longer to get this working.

/*
MIT License

Copyright (c) 2018 Sergey Matyukevich

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "sysregs.h"
#include "task/system_calls.h"

#define SYNC_INVALID_EL1t		0 
#define IRQ_INVALID_EL1t		1 
#define FIQ_INVALID_EL1t		2 
#define ERROR_INVALID_EL1t		3 

#define SYNC_INVALID_EL1h		4 
#define IRQ_INVALID_EL1h		5 
#define FIQ_INVALID_EL1h		6 
#define ERROR_INVALID_EL1h		7 

#define SYNC_INVALID_EL0_64	    8 
#define IRQ_INVALID_EL0_64	   	9 
#define FIQ_INVALID_EL0_64		10 
#define ERROR_INVALID_EL0_64	11 

#define SYNC_INVALID_EL0_32		12 
#define IRQ_INVALID_EL0_32		13 
#define FIQ_INVALID_EL0_32		14 
#define ERROR_INVALID_EL0_32	15 

#define SYNC_ERROR				16 
#define SYSCALL_ERROR			17 


//
//  Pretty simple IRQ kernel entry and exit routines below.  On entry, save all the registers on the stack.
//      On exit, restore the registers and execute the exception return.
//
//  The entry and exit routines optimize performance by reserving a block on the stack and then writing 
//      the CPU registers into that block in pairs (rather than push and pop).  Still we are saving a
//      bunch of registers, this is not super-lightweight....
//

#define EL0						0
#define EL1						1

#define S_FRAME_SIZE			288 		//  Space needed on stack to save all the registers - must match size of 'FullCPUState'

.macro	KernelEntry, el
    sub	sp, sp, #S_FRAME_SIZE
    stp	x0, x1, [sp, #16 * 0]
    stp	x2, x3, [sp, #16 * 1]
    stp	x4, x5, [sp, #16 * 2]
    stp	x6, x7, [sp, #16 * 3]
    stp	x8, x9, [sp, #16 * 4]
    stp	x10, x11, [sp, #16 * 5]
    stp	x12, x13, [sp, #16 * 6]
    stp	x14, x15, [sp, #16 * 7]
    stp	x16, x17, [sp, #16 * 8]
    stp	x18, x19, [sp, #16 * 9]
    stp	x20, x21, [sp, #16 * 10]
    stp	x22, x23, [sp, #16 * 11]
    stp	x24, x25, [sp, #16 * 12]
    stp	x26, x27, [sp, #16 * 13]
    stp	x28, x29, [sp, #16 * 14]

	.if	\el == 0
	mrs	x21, sp_el0
	.else
	add	x21, sp, #S_FRAME_SIZE
	.endif 

	mrs	x22, elr_el1    					//  For preemptive multi-tasking, save the ELR_EL1 and SPSR_EL1 registers as we can context switch in an IRQ Handler
	mrs	x23, spsr_el1

	stp	x30, x21, [sp, #16 * 15] 			//  Register 30 and the EL0 stack pointer
	stp	x22, x23, [sp, #16 * 16]			//  Program counter (more precisely, the EL1 link register) and pState (the EL1 Saved Program Status Register)

	mrs x22, tpidrro_el0
	mrs x23, tpidr_el1
	stp	x22, x23, [sp, #16 * 17]			//  The EL0 and EL1 thread pointers

.endm

.macro	KernelExit, el
	ldp	x22, x23, [sp, #16 * 17]
	msr	tpidrro_el0, x22
	msr	tpidr_el1, x23

	ldp	x22, x23, [sp, #16 * 16]
	ldp	x30, x21, [sp, #16 * 15] 

	.if	\el == 0
	msr	sp_el0, x21
	.endif 

	msr	elr_el1, x22						//  Restore the ELR_EL1 and SPSR_EL1 registers
	msr	spsr_el1, x23

    ldp	x0, x1, [sp, #16 * 0]
    ldp	x2, x3, [sp, #16 * 1]
    ldp	x4, x5, [sp, #16 * 2]
    ldp	x6, x7, [sp, #16 * 3]
    ldp	x8, x9, [sp, #16 * 4]
    ldp	x10, x11, [sp, #16 * 5]
    ldp	x12, x13, [sp, #16 * 6]
    ldp	x14, x15, [sp, #16 * 7]
    ldp	x16, x17, [sp, #16 * 8]
    ldp	x18, x19, [sp, #16 * 9]
    ldp	x20, x21, [sp, #16 * 10]
    ldp	x22, x23, [sp, #16 * 11]
    ldp	x24, x25, [sp, #16 * 12]
    ldp	x26, x27, [sp, #16 * 13]
    ldp	x28, x29, [sp, #16 * 14]
    add	sp, sp, #S_FRAME_SIZE		
    eret
.endm


.macro HandleInvalidEntry el, type
    KernelEntry \el

    //  Setup the call to show the invalid entry message

    mov	x0, #\type
    mrs	x1, esr_el1
    mrs	x2, elr_el1
    bl	ShowInvalidExceptionTableEntryMessage
    
    //  Just park the core, no recovery possible

    b ParkCore

.endm

//
//  Macro to define an exception vector table entry.  Interrupts are a subset of Exceptions in AARCH64. 
//      
//  Each vector table entry may be a maximum of 128 bytes (0x80) so the alignment for each
//      table entry is specified as 7 which results in 2^7 or 128 bytes per table entry.
//      
//  Each entry simply takes a label and places a 'branch to' label instruction into the table.
//

.macro	ExceptionVectorTableEntry	label
    .align	7
    b	\label
.endm

//	Define a table of exception vectors.  This table is used to dispatch exceptions to the appropriate handler.

.align	11
.globl __exception_vectors
__exception_vectors:

    //  This first set of four entries is for exceptions raised when execution is at EL1 and the SPSel register holds 0.
    //      When SPSel = 0, the stack pointer (SP) is shared between EL1 and EL0. 

	ExceptionVectorTableEntry	sync_invalid_el1t			// Synchronous EL1t
	ExceptionVectorTableEntry	irq_invalid_el1t            // IRQ EL1t
	ExceptionVectorTableEntry	fiq_invalid_el1t			// FIQ EL1t
	ExceptionVectorTableEntry	error_invalid_el1t			// Error EL1t

    //  This second set of four entries is for exceptions raised when execution is at EL1 and the SPSel register holds 1.
    //      When SPSel = 1, there are separate stack pointers (SP) for EL1 and EL0 (and EL2 and EL3 also).

	ExceptionVectorTableEntry	sync_invalid_el1h			// Synchronous EL1h
	ExceptionVectorTableEntry	EL1_IRQ		                // IRQ EL1h
	ExceptionVectorTableEntry	fiq_invalid_el1h			// FIQ EL1h
	ExceptionVectorTableEntry	error_invalid_el1h			// Error EL1h

    //  This first set of four entries is for exceptions raised when execution is at EL0 and the core is executing 64 bit code

	ExceptionVectorTableEntry	EL0_SynchronousException	// Synchronous 64-bit EL0
	ExceptionVectorTableEntry	EL0_IRQ						// IRQ 64-bit EL0
	ExceptionVectorTableEntry	fiq_invalid_el0_64			// FIQ 64-bit EL0
	ExceptionVectorTableEntry	error_invalid_el0_64		// Error 64-bit EL0

    //  This first set of four entries is for exceptions raised when execution is at EL0 and the core is executing 32 bit code

	ExceptionVectorTableEntry	sync_invalid_el0_32			// Synchronous 32-bit EL0
	ExceptionVectorTableEntry	irq_invalid_el0_32			// IRQ 32-bit EL0
	ExceptionVectorTableEntry	fiq_invalid_el0_32			// FIQ 32-bit EL0
	ExceptionVectorTableEntry	error_invalid_el0_32		// Error 32-bit EL0

//
//  Branch symbols for the exception vector table entries
//      Error conditions are fully enumerated.
//

sync_invalid_el1t:
	HandleInvalidEntry EL1, SYNC_INVALID_EL1t

irq_invalid_el1t:
	HandleInvalidEntry EL1, IRQ_INVALID_EL1t

fiq_invalid_el1t:
	HandleInvalidEntry EL1, FIQ_INVALID_EL1t

error_invalid_el1t:
	HandleInvalidEntry EL1, ERROR_INVALID_EL1t

sync_invalid_el1h:
	HandleInvalidEntry EL1, SYNC_INVALID_EL1h

irq_invalid_el1h:
	HandleInvalidEntry EL1, IRQ_INVALID_EL1h

fiq_invalid_el1h:
	HandleInvalidEntry EL1, FIQ_INVALID_EL1h

error_invalid_el1h:
	HandleInvalidEntry EL1, ERROR_INVALID_EL1h

sync_invalid_el0_64:
	HandleInvalidEntry EL0, SYNC_INVALID_EL0_64

irq_invalid_el0_64:
	HandleInvalidEntry EL0, IRQ_INVALID_EL0_64

fiq_invalid_el0_64:
	HandleInvalidEntry EL0, FIQ_INVALID_EL0_64

error_invalid_el0_64:
	HandleInvalidEntry EL0, ERROR_INVALID_EL0_64

sync_invalid_el0_32:
	HandleInvalidEntry EL0, SYNC_INVALID_EL0_32

irq_invalid_el0_32:
	HandleInvalidEntry EL0, IRQ_INVALID_EL0_32

fiq_invalid_el0_32:
	HandleInvalidEntry EL0, FIQ_INVALID_EL0_32

error_invalid_el0_32:
	HandleInvalidEntry EL0, ERROR_INVALID_EL0_32

EL1_IRQ:
	KernelEntry EL1
	bl	HandleIRQ
	KernelExit EL1

EL0_IRQ:
	KernelEntry EL0 
	bl	HandleIRQ
	KernelExit EL0 

EL0_SynchronousException:
	KernelEntry EL0
	mrs	x25, esr_el1										//	Read the syndrome register
	lsr	x24, x25, #ESR_ELx_EC_SHIFT							//	Exception class
	cmp	x24, #ESR_ELx_EC_SVC64								//	SVC in 64-bit state
	b.eq	EL0_SyscallDispatcher
	HandleInvalidEntry EL0, SYNC_ERROR

num_system_calls	.req	x25								//	number of system calls aliased to register 25
syscall_number		.req	x26								//	syscall number aliased to register 26
syscall_table_ptr	.req	x27								//	syscall table pointer aliased to register 27

EL0_SyscallDispatcher:
	adr	syscall_table_ptr, __system_call_table				//	load syscall table pointer
	uxtw	syscall_number, w8								//	syscall number in w8
	mov	num_system_calls, #NUMBER_OF_SYSTEM_CALLS
	bl	EnableIRQ
	cmp     syscall_number, num_system_calls        		//	check upper syscall limit
	b.hs	SyscallIndexOutOfRange

	ldr	x16, [syscall_table_ptr, syscall_number, lsl #3]	//	address in the syscall table
	blr	x16													//	call system_call routine
	bl	DisableIRQ				
	str	x0, [sp, 0]											// returned x0 - zero is offset of x0 in the stack frame
	KernelExit EL0

SyscallIndexOutOfRange:
	HandleInvalidEntry EL0, SYSCALL_ERROR

//
//	ReturnFromForkASMStub is called from the C 'TaskManager::ReturnFromFork()' function when new processes or threads are created.
//	

.globl ReturnFromForkASMStub
ReturnFromForkASMStub:
	cbz	x19, ReturnToUserSpace								//	If x19 is zero, return to EL0 which is user space
	mov	x0, x20
	blr	x19
ReturnToUserSpace:
	bl DisableIRQ				
	KernelExit EL0 

