// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

.equ LOCKED, 1
.equ UNLOCKED, 0


//
//  Mutex using Exclusive loads and stores
//
//  This Mutex will only work when the MMU is enabled.
//


.globl LockMutex
LockMutex:
    mrs	    x4, daif				// Hold DAIF flags
    msr	    daifset, #2				// Disable Irq
    mov	    w2, LOCKED
mutex_try_to_lock:
    ldaxr   w1, [x0]
    cmp     w1, LOCKED
    beq     mutex_wait_for_release
    stxr    w3, w2, [x0]
    cbnz    w3, mutex_try_to_lock
    dmb     ish
    msr     daif, x4                // Restore DAIF flags to whatever they were
    ret
mutex_wait_for_release:
    wfe
    b       mutex_try_to_lock

.globl UnlockMutex
UnlockMutex:
    mov     w1, UNLOCKED
	str	    w1, [x0]
	dmb     ish
    sev
	ret


//
//  Lighter weight spinlock using Exclusive loads and stores
//

.globl LockSpinLock
LockSpinLock:
    mov	    w2, LOCKED
	sevl
spinlock_wait_for_release:
	wfe
spinlock_try_to_lock:
	ldaxr	w1, [x0]
	cbnz	w1, spinlock_wait_for_release
	stxr	w1, w2, [x0]
	cbnz	w1, spinlock_try_to_lock
	ret


.globl UnlockSpinLock
UnlockSpinLock:
    mov     w1, UNLOCKED
	stlr	w1, [x0]           // Store zero to the spinlock, this command will also wake any cores waiting in wfe
	ret


