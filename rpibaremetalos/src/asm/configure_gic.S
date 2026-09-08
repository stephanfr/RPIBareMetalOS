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


//  Distributor bases differ per board; the CPU interface is always distributor + 0x1000
//      (RPi4: 0xFF841000/0xFF842000, RPi5/BCM2712: 0x10_7FFF9000/0x10_7FFFA000 -- from bcm2712.dtsi).
#define GIC_RPI4_DISTB		0xff841000
#define GIC_RPI5_DISTB		0x107FFF9000
#define GIC_CPUB_OFFSET		0x1000

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

	//	Select the distributor base by board type.  The CPU interface is always
	//		distributor + 0x1000 on both RPi4 and RPi5.
	ldr		x11, =GIC_RPI4_DISTB
	cmp		x0, #RPI_BOARD_ENUM_RPI5
	b.ne	gic_base_selected
	ldr		x11, =GIC_RPI5_DISTB

gic_base_selected:

	//	The distributor (GICD_CTLR + the SHARED SPI group registers) is configured on
	//		core 0 ONLY.  Having all four cores write these shared registers concurrently
	//		is what broke RPi5 boot.  Core 0 is the core whose full affinity Aff1|Aff0 is
	//		zero (correct on both the A72/Aff0 and A76/Aff1 layouts).
	mrs		x9, MPIDR_EL1
	and		x9, x9, #0xFFFF
	cbnz	x9, configure_per_core

	mov		w9, #3
	str		w9, [x11, #GICD_CTRLR]					//	Enable the distributor (Group 0 + Group 1)

	//	IGROUPR1..7 cover the SHARED SPIs (INTIDs 32-255) -> Group 1.  IGROUPR0 is
	//		handled per-core below because it is banked.
	add		x10, x11, #(GICD_IGROUPR + 4)
	mov		x9, #((IT_NR - 1) * 4)
	mov		w12, #~0

interrupt_grouping_loop :

	subs	x9, x9, #4
	str		w12, [x10, x9]
	b.ne	interrupt_grouping_loop

configure_per_core:

	//	Per-core banked config -- every core must run this for itself:
	//		IGROUPR0 (INTIDs 0-31: SGIs 0-15 + PPIs 16-31) is banked per-core, so each
	//		core must put its OWN SGIs into Group 1 or its IPIs never reach non-secure EL1.
	//		The CPU interface (GICC) is likewise per-core.
	mov		w9, #~0
	str		w9, [x11, #GICD_IGROUPR]				//	This core's IGROUPR0 -> Group 1

	add		x10, x11, #GIC_CPUB_OFFSET
	mov		w9, #0x1e7
	str		w9, [x10, #GICC_CTRLR]					//	Enable Group 1 IRQs from this core's CPU interface
	mov		w9, #0xff
	str		w9, [x10, #GICC_PMR]					//	Priority mask

	ret
