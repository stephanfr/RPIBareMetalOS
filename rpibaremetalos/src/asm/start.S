// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpu_part_nums.h"


#define BIT(x) (1 << (x))

#define SCR_RW			    BIT(10)
#define SCR_HCE			    BIT(8)
#define SCR_SMD			    BIT(7)
#define SCR_RES1_5		    BIT(5)
#define SCR_RES1_4		    BIT(4)
#define SCR_NS			    BIT(0)
#define SCR_VAL             (SCR_RW | SCR_HCE | SCR_SMD | SCR_RES1_5 | SCR_RES1_4 | SCR_NS)

#define ACTLR_VAL           (BIT(0) | BIT(1) | BIT(4) | BIT(5) | BIT(6))

#define CPUECTLR_EL1		S3_1_C15_C2_1
#define CPUECTLR_EL1_SMPEN	BIT(6)

#define L2CTLR_EL1		    S3_1_C11_C0_2

#define SPSR_EL3_D		    BIT(9)
#define SPSR_EL3_A		    BIT(8)
#define SPSR_EL3_I		    BIT(7)
#define SPSR_EL3_F		    BIT(6)
#define SPSR_EL3_MODE_EL2H	9
#define SPSR_EL3_VAL        (SPSR_EL3_D | SPSR_EL3_A | SPSR_EL3_I | SPSR_EL3_F | SPSR_EL3_MODE_EL2H)

#define SPSR_EL2_MASK_ALL 	(7 << 6)
#define SPSR_EL2_EL1h		(5 << 0)
#define SPSR_EL2_VALUE		(SPSR_EL2_MASK_ALL | SPSR_EL2_EL1h)


//
//  Register usage:
//
//  x0  - return value from function calls and local scratchpad register
//  x1  - local scratchpad register
//  x2  - local scratchpad register
//  x19 - local scratchpad register used for computing addresses
//  x20 - local scratchpad register used for holding the end address of the C++ statics to be initialized
//

.section ".text.boot"

.global _start

_start:

    //
    //  Set move to EL1
    //
    //      This will require some config at EL3 and EL2, which is dependent on the EL level when we enter this code.
    //      The default armstub configures EL3 and then jumps to this code in EL2.
    //      The armstub_base.bin with this project does not change the EL at all, so we will enter here at EL3.
    //

    //  Determine if we are running at EL3 - this can occur if the default ARMSTUB is replaced

    mrs     x0, CurrentEL
    and     x0, x0, #12                 // clear reserved bits

    cmp     x0, #12
    bne     running_in_el2              //  Not running on EL3, so skip to EL2 entry point

    //  Running at EL3 

<<<<<<< HEAD
    //  Identify the board type, we need that the setup interrupts and the physical timer

    bl      IdentifyBoardType
    bl      SetupPhysicalTimer

=======
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
	// Set L2 read/write cache latency to 3

	mrs     x0, L2CTLR_EL1
	mov     x1, #0x22
	orr     x0, x0, x1
	msr     L2CTLR_EL1, x0

	//  Enable Floating point instruction set and SIMD instructions

	msr     CPTR_EL3, xzr

	//  Set up SCR
	
    mov     x0, #SCR_VAL
	msr     SCR_EL3, x0

	//  Set up ACTLR

	mov     x0, #ACTLR_VAL
	msr     ACTLR_EL3, x0

	//  Set SMPEN

	mov     x0, #CPUECTLR_EL1_SMPEN
	msr     CPUECTLR_EL1, x0

<<<<<<< HEAD
    //  If we are on a platform that supports the Generic Interrupt Controller, configure it now

    bl      IdentifyBoardType
    cmp     x0, #RPI_BOARD_ENUM_RPI3
=======
    //  Identify the board type and then setup the physical timer

    bl      IdentifyBoardType
    mov     x3, x0                  //  Copy board type into register 3 

    bl      SetupPhysicalTimer

    //  If we are on a platform that supports the Generic Interrupt Controller, configure it now
    //      Register 3 contains the board type copied above. 

    cmp     x3, #RPI_BOARD_ENUM_RPI3
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    b.eq    rpi3_skip_gic_config

        bl      ConfigureGIC400
	
rpi3_skip_gic_config:

    //  Set up SCTLR_EL2
	//      All set bits below are res1. LE, no WXN/I/SA/C/A/M

	ldr     x0, =0x30c50830
	msr     SCTLR_EL2, x0
    
	//  Switch to EL2

	mov     x0, #SPSR_EL3_VAL
<<<<<<< HEAD
	msr     spsr_el3, x0
	adr     x0, running_in_el2
	msr     elr_el3, x0
=======
	msr     SPSR_EL3, x0
	adr     x0, running_in_el2
	msr     ELR_EL3, x0
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
	eret

    //  Running at EL2 - processor will be in EL2 on entry with the default ARMSTUB

running_in_el2:

    //  Read CPU id, stop slave cores

<<<<<<< HEAD
    mrs     x1, mpidr_el1
=======
    mrs     x1, MPIDR_EL1
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    and     x1, x1, #3
    cbz     x1, continue_on_core0

    //  CPU id > 0, stop

    b       ParkCore

    //  Continue execution only on core 0

continue_on_core0:

<<<<<<< HEAD
    //  The physical timer should be setup when we get here - either above or in the default armstub

    cmp     x0, #4
    beq     running_in_el1

    adrp    x19, __el1_stack_top
    add     x19, x19, :lo12:__el1_stack_top
    msr     sp_el1, x19                  //  Sets the stack for EL1 - this can only be done in EL2 or EL3

    //  Enable CNTP for EL1

    mrs     x0, cnthctl_el2
    orr     x0, x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    //  Disable coprocessor traps

    mov     x0, #0x33FF
    msr     cptr_el2, x0
    msr     hstr_el2, xzr
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
=======
    //  Sets the stack for EL1 - this can only be done in EL2 or EL3

    adrp    x19, __el1_stack_top
    add     x19, x19, :lo12:__el1_stack_top
    msr     SP_EL1, x19

    //  Enable CNTP for lower exception levels

    mrs     x0, CNTHCTL_EL2
    orr     x0, x0, #3
    msr     CNTHCTL_EL2, x0
    msr     CNTVOFF_EL2, xzr

    //  Disable various CPU architectural feature traps

    mov     x0, #0x33FF
    msr     CPTR_EL2, x0
    msr     HSTR_EL2, xzr
    mov     x0, #(3 << 20)
    msr     CPACR_EL1, x0
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    //  Enable AArch64 in EL1

    mov     x0, #(1 << 31)              // AArch64
    orr     x0, x0, #(1 << 1)           // SWIO hardwired on Pi3
<<<<<<< HEAD
    msr     hcr_el2, x0
    mrs     x0, hcr_el2

    //  Setup SCTLR access

    mov     x2, #0x0800
    movk    x2, #0x30d0, lsl #16
    msr     sctlr_el1, x2
=======
    msr     HCR_EL2, x0
    mrs     x0, HCR_EL2

    //  Configure CPU for EL1 and EL0

    mov     x2, #0x0800
    movk    x2, #0x30d0, lsl #16
    msr     SCTLR_EL1, x2
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    //  Change execution level to EL1

    mov     x2, SPSR_EL2_VALUE
<<<<<<< HEAD
    msr     spsr_el2, x2
    adr     x2, running_in_el1          //  Load the EL2 exception return reg with the running_in_el1 symbol so we go there on eret
    msr     elr_el2, x2
=======
    msr     SPSR_EL2, x2
    adr     x2, running_in_el1          //  Load the EL2 exception return reg with the running_in_el1 symbol so we go there on eret
    msr     ELR_EL2, x2
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    eret                                //  Exception level may only change on an exception return

running_in_el1:

    bl      IdentifyBoardType           //  Identify the board again in EL1 so we can store the value below

    adrp x19, __hw_board_type           //  Store the board type in the global __hw_board_type.  We do this here as EL3 has its own memory
    add x19, x19, :lo12:__hw_board_type //      which is not accessible from EL1.
    str w0, [x19]

    //  Set up exception handlers

	adr	    x0, _exception_vectors
<<<<<<< HEAD
	msr	    vbar_el1, x0		        //  Put the address of the exception vector table into VBAR_EL1
=======
	msr	    VBAR_EL1, x0		        //  Put the address of the exception vector table into VBAR_EL1
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    //  Set top of stack.
    //      The linker script defines the __stack symbol to be the top of OS RAM.
    //      Stack grows down toward the code.
    //
    //  When we set the SPSR_EL2 register to SPSR_EL2_VALUE above, the SPSR_EL2_EL1h bits are set which means
    //      that on the eret above, SPSel set to 1 for execution in EL1.  This matters as it will impact which 
    //      exception vector table entry is chosen when an exception (or interrupt) occurs.  If the exception
<<<<<<< HEAD
    //      is processed in EL1 with SPSel == 1, then the 't' suffixed entries are used.
    //
    //  SPSel == 1 means each exception level will have its own stack pointer (SP).  I *think* this makes sense
    //      from a security standpoint as it eliminates the risk of a stack based attack in EL0 getting into EL1.
=======
    //      is processed in EL1 with SPSel == 1, then the 'h' suffixed entries are used.
    //
    //  SPSel == 1 means each exception level will have its own stack pointer (SP).  I *think* this makes sense
    //      from a security standpoint as it eliminates a risk of a stack based attack in EL0 getting into EL1.
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    //      The flip side is that we will have to handle the stacks individually and need to insure we do not
    //      overrun them.  Not setting up the SPs correctly should be a catastrophic error on startup.

    adrp    x19, __el0_stack_top
    add     x19, x19, :lo12:__el0_stack_top
<<<<<<< HEAD
    msr     sp_el0, x19                          // Set the stack pointer for EL0
=======
    msr     SP_EL0, x19                          // Set the stack pointer for EL0
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    //  Clear bss
    //      TODO - make this more robust in the face of BSS segments not a multiple of 8 bytes in length

    adrp    x1, __bss_start
    add     x1, x1, :lo12:__bss_start
    ldr     w2, =__bss_size_in_double_words
    cbz     w2, initialize_cpp_statics      //  Skip the loop if the bss section is zero length

loop_for_bss_clear:

    str     xzr, [x1], #8                   //  Set 8 bytes at a time - the section in the linker script must match
    sub     w2, w2, #1
    cbnz    w2, loop_for_bss_clear

    //  Initialize the C++ statics

initialize_cpp_statics:

    adrp    x19, __init_array_start
    add     x19, x19, :lo12:__init_array_start
    adrp    x20, __init_array_end
    add     x20, x20, :lo12:__init_array_end
    cmp     x19, x20
    beq     branch_to_kernel_main

loop_for_next_static_initializer:
    
    ldr     x0, [x19], 8
    blr     x0
    cmp     x19, x20
    bne     loop_for_next_static_initializer

    //  Jump to the kernel main function, must have C style linkage

branch_to_kernel_main:
    
    bl      kernel_main

    //  If execution returns from the kernel main, then halt the core

    b       ParkCore

