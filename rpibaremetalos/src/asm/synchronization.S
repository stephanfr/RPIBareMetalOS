// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  UNLOCKED must be zero as we use the zero register to unlock the spinlock

.equ LOCKED, 1
.equ UNLOCKED, 0


//
//  Lightweight spinlock using Exclusive loads and stores
//

.globl LockSpinLock
LockSpinLock:
    mov	    w2, LOCKED
	sevl                            //  SEVL insures we fall through the WFE when we hit it next
spinlock_wait_for_release:
	wfe
spinlock_try_to_lock:
	ldaxr	w1, [x0]
	cbnz	w1, spinlock_wait_for_release
	stlxr	w1, w2, [x0]
	cbnz	w1, spinlock_try_to_lock
	ret

.globl UnlockSpinLock
UnlockSpinLock:
	stlr	wzr, [x0]                // Store zero to the spinlock, this command will also wake any cores waiting in wfe as a result of clearing the exclusive lock on the memory location
    ret

