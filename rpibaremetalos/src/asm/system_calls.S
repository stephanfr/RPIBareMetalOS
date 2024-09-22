// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/system_calls.h"

.globl sc_Write
sc_Write:
	mov w8, #SYS_WRITE_NUMBER	
	svc #0
	ret

.globl sc_Malloc
sc_Malloc:
	mov w8, #SYS_MALLOC_NUMBER	
	svc #0
	ret

.globl sc_Exit
sc_Exit:
	mov w8, #SYS_EXIT_NUMBER	
	svc #0
	ret

.globl sc_CloneTask
sc_CloneTask:
	//	Save arguments for the child in scratch registers.  We use them in thread_start below.
	
	//	x0 contains the const char* for the new name of the task.

	mov	x10, x1						//	Function pointer
	mov	x11, x2						//	Function argument

	mov	x1, x3						//	Stack
	mov x2, x4						//	Result Code pointer
	mov x3, x5						//	UUID pointer

	mov	x8, #SYS_CLONE_NUMBER
	svc	0x0							//	Synchronous exception for the system call

	cmp	x0, #SYS_CLONE_NEW_TASK		//	SYS_CLONE_NEW_TASK here means we are running inside a new task created with CloneTask() as it sets up a completely 
	beq	lblUserThreadStart			//		new full CPU state and sets x0 to SYS_CLONE_NEW_TASK.  If we have SYS_CLONE_NEW_TASK, then call the function with the arg.
	ret

lblUserThreadStart:
	mov	x29, 0						//	We are in the net-new task, so setup the argument and call the function.
	mov	x0, x11
	blr	x10

	//	Finished, the return value is on the stack
	mov	x8, #SYS_EXIT_NUMBER
	svc	0x0
