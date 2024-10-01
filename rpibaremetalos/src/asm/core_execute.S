// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  This function is C callable and is used to ask a specific core to come out of its start-up spin loop
//  and execute a function.  The core number is passed in x0 and the function pointer in x1.  This function
//  computes the correct slot in the core jump table for the core and places the function pointer there.

#include "core_states.h"

.globl CoreExecute;

CoreExecute:

	// Insure the core number is valid and if so, that the core is in the correct state

	ldr		x4, =__number_of_cores_available
	ldr		w5, [x4]
	cmp		w0, w5
	b.ge	.core_execute_error

	adrp    x4, __core_state
    add     x4, x4, :lo12:__core_state
	ldr		w5, [x4, x0, lsl #2]

	cmp		w5, #CORE_SPINNING_IN_EL1					// Core must be spinning in EL1	before we can ask it to execute code
	b.ne	.core_execute_error

	mov		w5, #CORE_JUMPED_TO_APPLICATION_CODE
	str		w5, [x4, x0, lsl #2]						// Mark the core as having jumped to application code

	adrp    x4, __core_jump_table
    add     x4, x4, :lo12:__core_jump_table
	str		x1, [x4, x0, lsl #3]						// Store the function pointer in the correct jump table slot

	sev													// Wake up the core

 	mov		x0, #1										// Return success
	ret

.core_execute_error:
	mov		x0, #0										// Return failure
	ret
