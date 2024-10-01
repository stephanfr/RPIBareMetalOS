// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpu_part_nums.h"
#include "core_states.h"


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
//  x3  - local scratchpad register
//  x4  - local scratchpad register
//  x19 - local scratchpad register used for computing addresses
//  x20 - local scratchpad register used for holding the end address of the C++ statics to be initialized
//  x21 - holds number of currently executing core
//  x22 - holds the board hardware id
//  x23 - holds the address of the core state table
//  x24 - holds the address of the core jump table
//  x25 - holds a flag to indicate if we arrived in EL3
//  x26 - holds the number of cores available
//

.section ".text.boot"

//  The standard ARMSTUB will move to eL2 and then spin cores 1->3 watching addresses in the table
//      at 'armstub_el2_spin_table' until a non-zero address is found.  If the minimal ARMSTUB with this
//      project is used, then all cores will enter here at EL3 and cores 1->3 will spin on the core jump table addresses.
//
//  I expect this to change when we get to RPIs with > 4 cores, as this table runs into the ARMSTUB magic number.

.equ armstub_el2_spin_table, 0xd8

//  Register aliases to make the code a bit more readable

x21_coreNumber .req x21
x22_boardHWId .req x22
x23_coreStateTableAddress .req x23
x24_coreJumpTableAddress .req x24
x25_arrivedInEL3 .req x25
x26_numberOfCoresAvailable .req x26

.macro InitializeRegisters
    mrs     x21_coreNumber, MPIDR_EL1
    and     x21_coreNumber, x21_coreNumber, #3
    bl      IdentifyBoardType
    mov     x22_boardHWId, x0
    adrp    x23_coreStateTableAddress, __core_state
    add     x23_coreStateTableAddress, x23_coreStateTableAddress, :lo12:__core_state
    adrp    x24_coreJumpTableAddress, __core_jump_table
    add     x24_coreJumpTableAddress, x24_coreJumpTableAddress, :lo12:__core_jump_table
.endm

.macro	SetCoreState, new_state
    mov     w0, \new_state
    str     w0, [x23_coreStateTableAddress, x21_coreNumber, lsl #2]
.endm

.macro	GetCoreState, core_number
    mov     x0, \core_number
    ldr     w0, [x23_coreStateTableAddress, x0, lsl #2]
.endm

//  Saves the board HW type in a global variable.  Only works in EL1

.macro SaveBoardHWType
    adrp x1, __hw_board_type
    add x1, x1, :lo12:__hw_board_type
    str x22_boardHWId, [x1]
.endm


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

    mov     x25_arrivedInEL3, #0

    mrs     x0, CurrentEL
    and     x0, x0, #12                             // clear reserved bits

    cmp     x0, #12
    bne     .running_in_el2                         //  Not running on EL3, so skip to EL2 entry point

    //  Running at EL3 

    mov     x25_arrivedInEL3, #1                    //  Set flag to indicate we arrived in EL3

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

    //  Identify the board type and then setup the physical timer

    bl      IdentifyBoardType
    mov     x22_boardHWId, x0                  //  Copy board type into register

    bl      SetupPhysicalTimer

    //  If we are on a platform that supports the Generic Interrupt Controller, configure it now
    //      Register 3 contains the board type copied above. 

    cmp     x22_boardHWId, #RPI_BOARD_ENUM_RPI3
    b.eq    .rpi3_skip_gic_config

    bl      ConfigureGIC400
	
.rpi3_skip_gic_config:

    //  Set up SCTLR_EL2
	//      All set bits below are res1. LE, no WXN/I/SA/C/A/M

	ldr     x0, =0x30c50830
	msr     SCTLR_EL2, x0
    
	//  Switch to EL2

	mov     x0, #SPSR_EL3_VAL
	msr     SPSR_EL3, x0
	adr     x0, .running_in_el2
	msr     ELR_EL3, x0
	eret

    //  Running at EL2 - processor will be in EL2 on entry with the default ARMSTUB

.running_in_el2:

    //  Setup the local registers holding the core number, etc

    InitializeRegisters

    //  For now, we know the platform will have at most 4 cores

//    adrp    x1, __number_of_cores_available
//    add     x1, x1, :lo12:__number_of_cores_available
//    mov     x0, #4
//    str     w0, [x1]

//    mov     x26_numberOfCoresAvailable, x0

    //  Update the core state

    SetCoreState #CORE_STARTED_IN_EL2

    //  Spin on all but Core 0

    cbz     x21_coreNumber, .configure_stacks_for_core

    //  Spin on cores 1->3

    SetCoreState #CORE_SPINNING_IN_EL2

.el2_spin:
	wfe
	ldr x4, [x24_coreJumpTableAddress, x21_coreNumber, lsl #3]
	cbz x4, .el2_spin

    //  Clear the jump table address for the current core - we will use it again to jump to the application code

    mov x0, #0
	str x0, [x24_coreJumpTableAddress, x21_coreNumber, lsl #3]

	mov x1, #0
	mov x2, #0
	mov x3, #0
	br x4

.configure_stacks_for_core :

    SetCoreState #CORE_CONFIGURING_STACKS_IN_EL2

    //  Set up the stack locations for this core

    adrp    x1, el1_stack_top
    add     x1, x1, :lo12:el1_stack_top
    ldr     x0, [x1, x21_coreNumber, lsl #3] 
    msr     SP_EL1, x0

    adrp    x1, el0_stack_top
    add     x1, x1, :lo12:el0_stack_top
    ldr     x0, [x1, x21_coreNumber, lsl #3] 
    msr     SP_EL0, x0

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

    //  Enable AArch64 in EL1

    mov     x0, #(1 << 31)              // AArch64
    orr     x0, x0, #(1 << 1)           // SWIO hardwired on Pi3
    msr     HCR_EL2, x0
    mrs     x0, HCR_EL2

    //  Configure CPU for EL1 and EL0

    mov     x2, #0x0800
    movk    x2, #0x30d0, lsl #16
    msr     SCTLR_EL1, x2

    //  Change execution level to EL1

    mov     x2, SPSR_EL2_VALUE
    msr     SPSR_EL2, x2
    adr     x2, .running_in_el1             //  Load the EL2 exception return reg with the .running_in_el1 symbol so we go there on eret
    msr     ELR_EL2, x2

    eret                                    //  Exception level may only change on an exception return

.running_in_el1:

    //  Set up exception handlers

	adr	    x0, __exception_vectors
	msr	    VBAR_EL1, x0		            //  Put the address of the exception vector table into VBAR_EL1

    //  When we set the SPSR_EL2 register to SPSR_EL2_VALUE above, the SPSR_EL2_EL1h bits are set which means
    //      that on the eret above, SPSel set to 1 for execution in EL1.  This matters as it will impact which 
    //      exception vector table entry is chosen when an exception (or interrupt) occurs.  If the exception
    //      is processed in EL1 with SPSel == 1, then the 'h' suffixed entries are used.
    //
    //  SPSel == 1 means each exception level will have its own stack pointer (SP).  I *think* this makes sense
    //      from a security standpoint as it eliminates a risk of a stack based attack in EL0 getting into EL1.
    //      The flip side is that we will have to handle the stacks individually and need to insure we do not
    //      overrun them.  Not setting up the SPs correctly should be a catastrophic error on startup.

    //  Continue on core zero, all other cores will spin until they get a jump address

    cbz x21_coreNumber, .continue_on_core0

    //  Spin on the other cores until they get a jump address - probably from CoreExecute

    SetCoreState #CORE_SPINNING_IN_EL1

.el1_spin:
	wfe
	ldr x4, [x24_coreJumpTableAddress, x21_coreNumber, lsl #3]
	cbz x4, .el1_spin

	mov x0, #0
	mov x1, #0
	mov x2, #0
	mov x3, #0
	br x4

    //  We should never get here, but if we do, park the core

    b ParkCore

    //  Continue execution only on core 0

.continue_on_core0:

    SetCoreState #CORE_INITIALIZING_KERNEL

    //  Save the board HW type in a global C variable so the C code may access it.

    SaveBoardHWType

    //  Clear bss
    //      TODO - make this more robust in the face of BSS segments not a multiple of 8 bytes in length

    adrp    x1, __bss_start
    add     x1, x1, :lo12:__bss_start
    ldr     w2, =__bss_size_in_double_words
    cbz     w2, .initialize_cpp_statics             //  Skip the loop if the bss section is zero length

loop_for_bss_clear:

    str     xzr, [x1], #8                           //  Set 8 bytes at a time - the section in the linker script must match
    sub     w2, w2, #1
    cbnz    w2, loop_for_bss_clear

    //  Initialize the C++ statics
    //      Use registers x19 and x20 to hold the start and end addresses of the C++ statics to be initialized
    //      as the intializers may modify registers below 19.  Registers 19->28 are 'callee saved' registers
    //      so the called functions are required to preserve their values.
    //
    //  There is an array of function pointers that are called to initialize the C++ statics.  The array is
    //      defined in the linker script and the start and end addresses are passed to this code in the
    //      using symbols.  The code below loads the start and end addresses and loops over each - so long as
    //      there are > 0 initializers.

.initialize_cpp_statics:

    adrp    x19, __init_array_start
    add     x19, x19, :lo12:__init_array_start
    adrp    x20, __init_array_end
    add     x20, x20, :lo12:__init_array_end
    cmp     x19, x20
    beq     .start_additional_cores

.loop_for_next_static_initializer:
    
    ldr     x0, [x19], 8                                //  Load the next initializer function address into x0
    blr     x0                                          //  Call the initializer function
    cmp     x19, x20
    bne     .loop_for_next_static_initializer

    //  Bring the other cores out of their spin loops

.start_additional_cores:

    mov x19, #1                                         //  Start with core 1

.start_next_core:
    mov x0, x19
    bl StartCore
    add x19, x19, #1
    cmp x19, #4                 //  Loop until all cores are started
    b.lo .start_next_core

    //  Jump to the kernel main function, must have C style linkage

    SetCoreState #CORE_JUMPED_TO_KERNEL_MAIN

    bl      kernel_main

    //  If execution returns from the kernel main, then halt the core

    b       ParkCore


//
//  Function to start a secondary core
//

.global StartCore
StartCore:

    mov x3, x0

    cmp x25_arrivedInEL3, #1                            //  If we arrived in EL3, then we do not need to release cores from the standard ARMSTUB
    b.eq .wait_core_ack

    mov x1, #armstub_el2_spin_table					    //  Armstub EL2 spin table
	ldr x2, =.running_in_el2				            //  Function pointer
	str	x2, [x1, x3, lsl #3]				            //  Store the function address to core to start
	sev										            //  Wake up core

.wait_core_ack:
	GetCoreState x3
	cmp	w0, #CORE_SPINNING_IN_EL2			            //  Wait for the core to be spinning in the startup code in EL2
	bne	.wait_core_ack

	ldr x2, =.configure_stacks_for_core		            //  We want to configure the stacks next
	str	x2, [x24_coreJumpTableAddress, x3, lsl #3]      //  Store the function address to the core
	sev										            //  Wake up core

    ret                                                 //  Return

//
//  Values for stack pointers
//

el1_stack_top: .quad __core0_el1_stack_top
               .quad __core1_el1_stack_top
               .quad __core2_el1_stack_top
               .quad __core3_el1_stack_top  

el0_stack_top: .quad __core0_el0_stack_top
               .quad __core1_el0_stack_top
               .quad __core2_el0_stack_top
               .quad __core3_el0_stack_top

