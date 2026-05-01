// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  This function is C callable and is used to ask a specific core to come out of its start-up spin loop
//  and execute a function.  The core number is passed in x0 and the function pointer in x1.  This function
//  computes the correct slot in the core jump table for the core and places the function pointer there.

#include "processor_cores.h"

.align 4
.globl CoreExecute;

CoreExecute:

	// Insure the core number is valid and the core is in the correct state

	ldr		x9, =__number_of_cores_available
	ldr		w10, [x9]
	cmp		w0, w10
	b.ge	core_execute_error

	adrp    x9, __core_state
    add     x9, x9, :lo12:__core_state
	add		x11, x9, x0, lsl #2							// 	Compute address of the state slot (4 bytes per core)
	dc 		civac, x11									// 	Invalidate Core 0's cached copy so the next load reads from DRAM
	dsb		sy											//	ARM ARM requires DSB after DC CIVAC before a dependent load
	ldr		w10, [x11]

	cmp		w10, #CORE_SPINNING_IN_EL1					// 	Core must be spinning in EL1 before we can ask it to execute code
	b.ne	core_execute_error

	adrp    x9, __core_jump_table
    add     x9, x9, :lo12:__core_jump_table
	mov		x0, x0, lsl #3								// 	Compute the address of the correct jump table slot (8 bytes per core)
	add		x9, x9, x0									// 	Compute the address of the correct jump table slot
	str		x1, [x9]									// 	Store the function pointer in the correct jump table slot
	dc 		civac, x9									// 	Flush the function pointer to DRAM so the spinning core sees it
	dsb		sy											// 	Ensure the flush and store are visible before SEV

	sev													// 	Wake up the core

 	mov		x0, #1										// 	Return success
	ret

core_execute_error:
	mov		x0, #0										// 	Return failure
	ret

