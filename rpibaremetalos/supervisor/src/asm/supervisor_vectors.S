// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

.macro MonitorVectorEntry label
	.align 7
	b	\label
.endm

.section ".vectors"
.align 11
.globl __monitor_vectors
__monitor_vectors:

	MonitorVectorEntry MonitorParkCore        // Current EL, SP0,  Synchronous
	MonitorVectorEntry MonitorParkCore        // Current EL, SP0,  IRQ
	MonitorVectorEntry MonitorParkCore        // Current EL, SP0,  FIQ
	MonitorVectorEntry MonitorParkCore        // Current EL, SP0,  SError

	MonitorVectorEntry MonitorParkCore        // Current EL, SPx,  Synchronous
	MonitorVectorEntry MonitorParkCore        // Current EL, SPx,  IRQ
	MonitorVectorEntry MonitorParkCore        // Current EL, SPx,  FIQ
	MonitorVectorEntry MonitorParkCore        // Current EL, SPx,  SError

	MonitorVectorEntry MonitorSynchronous64   // Lower EL, AArch64, Synchronous  <- SMC
	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch64, IRQ
	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch64, FIQ
	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch64, SError

	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch32, Synchronous
	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch32, IRQ
	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch32, FIQ
	MonitorVectorEntry MonitorParkCore        // Lower EL, AArch32, SError

.section ".text"

#define ESR_EC_SMC64 0x17

MonitorSynchronous64:

	//  SMCCC: x0-x3 carry arguments and results and x4-x17 are volatile, but x18-x30 and SP
	//      must come back unchanged.  The C callee preserves x19-x28 per AAPCS64; x18 is the
	//      platform register and x29/x30 are ours to save.
	stp     x29, x30, [sp, #-32]!
	str     x18, [sp, #16]

	mrs     x9, ESR_EL3
	lsr     x9, x9, #26
	cmp     x9, #ESR_EC_SMC64
	b.ne    MonitorParkCore

	bl      MonitorHandleSMC                // x0-x3 in, result in x0

	ldr     x18, [sp, #16]
	ldp     x29, x30, [sp], #32

	//  ELR_EL3 already points past the SMC - nothing to adjust.
	eret

MonitorParkCore:
	wfe
	b       MonitorParkCore