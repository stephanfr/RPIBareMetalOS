// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

.equ LOCKED, 1
.equ UNLOCKED, 0

.globl LockMutex
LockMutex:
    mrs	    x4, daif				// Hold DAIF flags
    msr	    daifset, #2				// Disable Irq
    mov	    w2, LOCKED
.mutex_try_to_lock_mutex:
    ldaxr   w1, [x0]
    cmp     w1, LOCKED
    beq     .mutex_wait_for_release
    stxr    w3, w2, [x0]
    cbnz    w3, .mutex_try_to_lock_mutex
    dmb     ish
    msr     daif, x4                // Restore DAIF flags to whatever they were
    ret
.mutex_wait_for_release:
    wfe
    b       .mutex_try_to_lock_mutex

.globl UnlockMutex
UnlockMutex:
    mov     w1, UNLOCKED
	str	    w1, [x0]
	dmb     ish
    sev
	ret
