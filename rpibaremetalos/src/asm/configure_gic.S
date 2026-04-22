// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


//  The GIC-400 Generic Interrupt Controller (GIC) is present on the RPI4 and later.
//		We want to use the GIC for the RPI4 and beyond, so it must be enabled.
//		The RPI3 does not have the GIC-400, so the code below should not be
//		invoked for it.  That test is done in the armstub code.
//
//   The GIC can only be enabled when the processor is in secure mode, i.e. EL3.
//
//  This configuration is simple, we set all interrupts to group 1, then we enable the GIC.  I think this is 
//      a standard configuration.


#include "cpu_part_nums.h"


#define GIC_DISTB		0xff841000
#define GIC_CPUB		0xff842000

#define GICC_CTRLR	    0x0
#define GICC_PMR	    0x4
#define IT_NR		    0x8     			// Number of interrupt enable registers (8 32 bit registers for a total of 256 interrupts)
#define GICD_CTRLR	    0x0
#define GICD_IGROUPR	0x80


.global ConfigureGIC400

ConfigureGIC400:

	//	We will be a bit pathological here and double-check that the board is correct and we are in secure mode.

    mrs     x9, CurrentEL					//	Insure we are in secure mode
    and     x9, x9, #12                 	// 	Clear reserved bits

    cmp     x9, #12
    b.eq    test_for_gic_400              	//  Running on EL3, test for the board type next

	ret										//	Not running in EL3, return now

test_for_gic_400:

    cmp     x0, #RPI_BOARD_ENUM_RPI3
	b.ne 	has_gic_400						//	Board has a GIC-400

	ret										//	Board does not have a GIC-400, so return now

has_gic_400:

	mrs		x9, MPIDR_EL1
	ldr		x11, =GIC_DISTB
	tst		x9, #0x3
	b.eq	skip_to_for_core_zero					//	Skip next for core 0

	mov		w9, #3									//	Enable Group 0 and 1 IRQs from distributor for all cores except core 0
	str		w9, [x11, #GICD_CTRLR]

skip_to_for_core_zero:

	add		x10, x11, #(GIC_CPUB - GIC_DISTB)
	mov		w9, #0x1e7
	str		w9, [x10, #GICC_CTRLR]					//	Enable Group 1 IRQs from CPU interface
	mov		w9, #0xff
	str		w9, [x10, #GICC_PMR]					//	Priority mask
	add		x11, x11, #GICD_IGROUPR
	mov		x9, #(IT_NR * 4)
	mov		w10, #~0								//	Put all interrupts into Group 1

interrupt_grouping_loop :

	subs	x9, x9, #4
	str		w10, [x11, x9]
	b.ne	interrupt_grouping_loop

	ret
