// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpu_part_nums.h"

//
//  Constants for setting up the physical timer on BCM2837 or BCM2711 SOCs
//

//  BCM2837

#define BCM2837_LOCAL_CONTROL		0x40000000
#define BCM2837_LOCAL_PRESCALER		0x40000008

#define BCM2837_OSC_FREQ		    19200000


//  BCM2711

#define BCM2711_LOCAL_CONTROL		0xff800000
#define BCM2711_LOCAL_PRESCALER		0xff800008

#define BCM2711_OSC_FREQ		    54000000


//
//  The following function sets up the physical timer on the board.  The setup
//      depends on the SOC type but has the same general configuration:
//
//      LOCAL_CONTROL:
//          Bit 9 clear: Increment by 1 (instead of incrementing by 2 is the bit were set)
//          Bit 8 clear: Timer source is the onboard CPU crystal frequency (not the APB)
//
//      LOCAL_PRESCALER:
//          Configured to divide by 1
//
//      CNTFRQ_EL0:
//          Oscillator Frequency - depends on the SOC
//
//
//  Arguments :
//
//      x0 - enum indicating the board type
//
//  Returns :
//
//      1 on success
//      0 on failure


.global SetupPhysicalTimer

SetupPhysicalTimer :

    cmp x0, #RPI_BOARD_ENUM_RPI3
    b.ne spt_notRpi3                            //  Jump to test for different board if this is not an RPI3

    //  BCM2837 follows 
	
	ldr x0, =BCM2837_LOCAL_CONTROL
	str wzr, [x0]

	mov w1, 0x80000000
	str w1, [x0, #(BCM2837_LOCAL_PRESCALER - BCM2837_LOCAL_CONTROL)]

	//  Set the oscillator frequency

	ldr x0, =BCM2837_OSC_FREQ
	msr CNTFRQ_EL0, x0

    ldr x0, =0
    msr CNTKCTL_EL1, x0

    ldr x0, =3
    msr CNTP_CTL_EL0, x0                        //  Enable the timer but without interrupts

    msr CNTVOFF_EL2, xzr                        //  No virtual offset

    ldr x0, =1                                  //  Return Success

    ret

    //  Not the RPI 3 if we get below

spt_notRpi3 :

    cmp x0, #RPI_BOARD_ENUM_RPI4                //  Board type will still be in x0
    b.ne spt_notRpi4

    //  BCM2711 follows 
	
	ldr x0, =BCM2711_LOCAL_CONTROL
	str wzr, [x0]

	mov w1, 0x80000000
	str w1, [x0, #(BCM2711_LOCAL_PRESCALER - BCM2711_LOCAL_CONTROL)]

	//  Set the oscillator frequency

	ldr x0, =BCM2711_OSC_FREQ
	msr CNTFRQ_EL0, x0

    ldr x0, =0
    msr CNTKCTL_EL1, x0

    ldr x0, =3
    msr CNTP_CTL_EL0, x0                        //  Enable the timer but without interrupts

    msr CNTVOFF_EL2, xzr                        //  No virtual offset

    ldr x0, =1                                  //  Return Success

    ret

    //  Not RPI3 or RPI4 if we get below, so return a failed result

spt_notRpi4 :

    ldr x0, =0                                  //  Return Failure
    ret
