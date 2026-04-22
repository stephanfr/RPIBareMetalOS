// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpu_part_nums.h"
#include "processor_cores.h"
#include "os_memory_config.h"
#include "enable_disable_irqs.h"


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
//  x9  - local scratchpad register
//  x10 - local scratchpad register
//  x11 - local scratchpad register
//  x12 - local scratchpad register
//  x13 - local scratchpad register
//  x14 - local scratchpad register
//  x15 - local scratchpad register used in macros to avoid tripping up state in the rest of the code
//  x19 - holds number of currently executing core
//  x20 - holds the board hardware id
//  x21 - holds the address of the core state table
//  x22 - holds the address of the core jump table
//  x23 - holds a flag to indicate if we arrived in EL3
//  x24 - holds the number of cores available
//  x25 - used for local variables that will not be changed by subroutine calls
//  x26 - used for local variables that will not be changed by subroutine calls
//

.section ".text.boot"

//  The standard ARMSTUB will move to eL2 and then spin cores 1->3 watching addresses in the table
//      at 'armstub_el2_spin_table' until a non-zero address is found.  If the minimal ARMSTUB with this
//      project is used, then all cores will enter here at EL3 and cores 1->3 will spin on the core jump table addresses.
//
//  I expect this to change when we get to RPIs with > 4 cores, as this table runs into the ARMSTUB magic number.

.equ armstub_el2_spin_table, 0xd8

//  Register aliases to make the code a bit more readable

x19_coreNumber .req x19
x20_boardHWId .req x20
x21_coreStateTableAddress .req x21
x22_coreJumpTableAddress .req x22
x23_arrivedInEL3 .req x23
x24_numberOfCoresAvailable .req x24



.macro InitializeLocalRegisterVariables
    mrs     x19_coreNumber, MPIDR_EL1
    and     x19_coreNumber, x19_coreNumber, #3
    bl      IdentifyBoardType
    mov     x20_boardHWId, x0
    bl      DetermineNumberOfCores
    mov     x24_numberOfCoresAvailable, x0
    adrp    x21_coreStateTableAddress, __core_state
    add     x21_coreStateTableAddress, x21_coreStateTableAddress, :lo12:__core_state
    adrp    x22_coreJumpTableAddress, __core_jump_table
    add     x22_coreJumpTableAddress, x22_coreJumpTableAddress, :lo12:__core_jump_table
.endm

.macro	SetCoreState, new_state
    mov     w0, \new_state
    mov     x15, x21_coreStateTableAddress
    add     x15, x15, x19_coreNumber, lsl #2
    str     w0, [x15]
    dc 		civac, x15
.endm

.macro	GetCoreState, core_number
    mov     x0, \core_number
    mov     x15, x21_coreStateTableAddress
    add     x15, x15, x0, lsl #2
    dc 		civac, x15
    ldr     w0, [x15]
.endm

//  Saves the board HW type in a global variable.  Only works in EL1

.macro SaveBoardHWType
    adrp    x15, __hw_board_type
    add     x15, x15, :lo12:__hw_board_type
    str     x20_boardHWId, [x15]
.endm

//  Saves the number of cores available.  Only works in EL1

.macro SaveNumberOfCoresAvailable
    adrp    x15, __number_of_cores_available
    add     x15, x15, :lo12:__number_of_cores_available
    str     x24_numberOfCoresAvailable, [x15]
.endm

//
//  In the code below, be cautious about alignment for any points to which the code will jump.
//      There are align directives inline before key locations, but if the code is modified with more jump points
//      then additional align directives may be needed.  If this is tripped up, then the code will work fine when
//      the jump points align correctly but then crash when a new instruction is added that throws off alignment.
//
//  The code is also pretty run-on, I have added macros and a couple subroutines but mostly this is just linear
//      intialization code with various internal jump points.  I could break it up more but I doubt that would
//      improve readability or flow.
//
//  There is a state machine, albeit a very simple one.  Core 0 does most of the heavy lifting, including bringing
//      up the other cores.  Depending on the EL we enter, then either all secondary cores will arrive here in EL3
//      or they will be spinning in EL2 and have to be released using a low memory location specified by the ARMSTUB
//      to provide a jump address, when core 0 is ready to release them.  If all the cores arrive here in EL3, as a
//      result of using a minimal ARMSTUB, then we skip the low memory location jump address and the cores will eventually
//      spin in EL2 in this code. They then wait for core 0 to give them a jump address - which is currently an entry point for the
//      cores to configure their stacks and complete control register initialization - before rendezvousing in spins in EL1
//      waiting for another jump address, this time most likely from a call to CoreExecute() in the C/C++ code.
//
//  Only Core 0 will make it to KernelMain, all other cores will spin in EL1 until they get a jump address to a different
//      void() function in the C code.  The first part of KernelMain is also one-time initialization code.
//
//  In general all cores get the same control register configuration, the only real difference is that core 0
//      initializes the C++ statics and bss space before moving the other cores to EL1 spin loops and going to KernelMain.
//

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

    mov     x23_arrivedInEL3, #0

    mrs     x9, CurrentEL
    and     x9, x9, #12                             // clear reserved bits

    cmp     x9, #12
    bne     running_in_el2                         //  Not running on EL3, so skip to EL2 entry point

    //  Running at EL3 

    mov     x23_arrivedInEL3, #1                    //  Set flag to indicate we arrived in EL3

	// Set L2 read/write cache latency to 3

	mrs     x9, L2CTLR_EL1
	mov     x10, #0x22
	orr     x9, x9, x10
	msr     L2CTLR_EL1, x9

	//  Enable Floating point instruction set and SIMD instructions

	msr     CPTR_EL3, xzr

	//  Set up SCR
	
    mov     x9, #SCR_VAL
	msr     SCR_EL3, x9

	//  Set up ACTLR

	mov     x9, #ACTLR_VAL
	msr     ACTLR_EL3, x9

	//  Set SMPEN

	mov     x9, #CPUECTLR_EL1_SMPEN
	msr     CPUECTLR_EL1, x9

    //  Identify the board type and then setup the physical timer

    bl      IdentifyBoardType
    mov     x20_boardHWId, x0                                   //  Copy board type into register

    bl      SetupPhysicalTimer                                  //  Board HW id must be passed in x0

    //  If we are on a platform that supports the Generic Interrupt Controller, configure it now
    //      Register 3 contains the board type copied above. 

    cmp     x20_boardHWId, #RPI_BOARD_ENUM_RPI3
    b.eq    rpi3_skip_gic_config

    bl      ConfigureGIC400
	
rpi3_skip_gic_config:

    //  Set up SCTLR_EL2
	//      All set bits below are res1. LE, no WXN/I/SA/C/A/M

	ldr     x9, =0x30c50830
	msr     SCTLR_EL2, x9
    
	//  Switch to EL2

	mov     x9, #SPSR_EL3_VAL
	msr     SPSR_EL3, x9
	adr     x9, running_in_el2
	msr     ELR_EL3, x9
	eret

    //  Running at EL2 - processor will be in EL2 on entry with the default ARMSTUB

.align 4                                                        //  Align the code to a 4 byte boundary
running_in_el2:

    //  Setup the local registers holding the core number, etc

    InitializeLocalRegisterVariables

    //  Update the core state

    SetCoreState #CORE_STARTED_IN_EL2

    //  Spin on all but Core 0

    cbz     x19_coreNumber, configure_stacks_for_core                          //  Core zero will build the MMU tables.  The other cores will just point to them.

    //  Spin on cores 1->3

    SetCoreState #CORE_SPINNING_IN_EL2

el2_spin:
	wfe
	add     x15, x22_coreJumpTableAddress, x19_coreNumber, lsl #3
	dc      civac, x15
	ldr     x9, [x15]
	cbz     x9, el2_spin

    //  Clear the jump table address for the current core - we will use it again to jump to the application code

    mov     x10, #0
	str     x10, [x15]

	br      x9

.align 4                                                        //  Align the code to a 4 byte boundary
configure_stacks_for_core :

    SetCoreState #CORE_CONFIGURING_STACKS_IN_EL2

    //  Set up the stack locations for this core

    mov     x9, #EL1_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES
    mov     x10, #EL0_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES

    add     x11, x9, x10
    mul     x11, x11, x19_coreNumber

    adrp    x12, __per_core_initialization_stack_bottom
    add     x12, x12, :lo12:__per_core_initialization_stack_bottom
    add     x12, x12, x11
    add     x12, x12, x9
    msr     SP_EL1, x12
    add     x12, x12, x10
    msr     SP_EL0, x12

    //  Enable CNTP for lower exception levels

    mrs     x9, CNTHCTL_EL2
    orr     x9, x9, #3
    msr     CNTHCTL_EL2, x9
    msr     CNTVOFF_EL2, xzr

    //  Disable various CPU architectural feature traps

    mov     x9, #0x33FF
    msr     CPTR_EL2, x9
    msr     HSTR_EL2, xzr
    mov     x9, #(3 << 20)
    msr     CPACR_EL1, x9

    //  Enable AArch64 in EL1

    mov     x9, #(1 << 31)                                      // AArch64
    orr     x9, x9, #(1 << 1)                                   // SWIO hardwired on Pi3
    msr     HCR_EL2, x9
    mrs     x9, HCR_EL2

    //  Configure CPU for EL1 and EL0

    mov     x9, #0x0800
    movk    x9, #0x30d0, lsl #16
	orr     x9, x9, #(0x1 << 12)                                // Enable the instruction cache (D-cache enabled with MMU below)
    msr     SCTLR_EL1, x9

    //  Change execution level to EL1

    mov     x9, SPSR_EL2_VALUE
    msr     SPSR_EL2, x9
    adr     x9, running_in_el1                                  //  Load the EL2 exception return reg with the running_in_el1 symbol so we go there on eret
    msr     ELR_EL2, x9

    eret                                                        //  Exception level may only change on an exception return

running_in_el1:

    //  Set up exception handlers

	adr	    x9, __exception_vectors
	msr	    VBAR_EL1, x9		                                //  Put the address of the exception vector table into VBAR_EL1

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

    cbz x19_coreNumber, continue_on_core0

    //  Spin on the other cores until they get a jump address - probably from CoreExecute

    SetCoreState #CORE_SPINNING_IN_EL1

    mov     x10, x22_coreJumpTableAddress
    add     x10, x10, x19_coreNumber, lsl #3        //  Compute the address of the correct jump table slot, we need this in a register for the cache line invalidation  

el1_spin:
	wfe
    dc 		civac, x10                               //  Invalidate the cache line containing the jump address as it is written by the master core
	ldr     x9, [x10]
	cbz     x9, el1_spin

    //  Enable the memory manager on the core

    adrp    x0, __early_l1_page_table
    add     x0, x0, :lo12:__early_l1_page_table
    mov     x1, #0
    bl      EnableMMUTables

    //  Mark the core as having jumped to application code

    SetCoreState #CORE_JUMPED_TO_SECONDARY_MAIN

    //  Jump to the function pointed to by the jump table

	mov     x1, #0                                                  //  Clear the parameter registers
	mov     x2, #0
	mov     x3, #0
	mov     x4, #0
	mov     x5, #0
	mov     x6, #0
	mov     x7, #0

	add     x15, x22_coreJumpTableAddress, x19_coreNumber, lsl #3
	ldr     x9, [x15]
	br      x9

    //  We should never get here, but if we do, park the core

    b       ParkCore

    //  Continue execution only on core 0

continue_on_core0:

    SetCoreState #CORE_INITIALIZING_KERNEL

    //  Save BEFORE GetBootTimeSettings: that function reads __hw_board_type from memory
    //  to select the MMIO base address.  Without this, __hw_board_type is 0 and the
    //  function calls park_core_on_error, producing no output.
    //  Note: BSS clear below will zero these globals again; they are re-saved afterwards.

    SaveBoardHWType
    SaveNumberOfCoresAvailable

    //  Get the board version, VC memory start, and kernel command line

    bl GetBootTimeSettings

    //  Enable the MMU before clearing BSS or running any C++ static constructors.
    //  Conservative 1:1 identity map (see SetupEarlyPageTables in mmu.S for region breakdown).
    //  InitializePlatform() will replace these tables with final platform-specific ones.

    bl      SetupEarlyPageTables

    adrp    x0, __early_l1_page_table
    add     x0, x0, :lo12:__early_l1_page_table
    mov     x1, #0                                              //  TTBR1 unused; TTBR0 covers all
    bl      EnableMMUTables
    //  MMU, D-cache, and I-cache are now active with 1:1 identity mapping.

    //  Clear bss
    //      TODO - make this more robust in the face of BSS segments not a multiple of 8 bytes in length

    adrp    x9, __bss_start
    add     x9, x9, :lo12:__bss_start
    ldr     w10, =__bss_size_in_double_words
    cbz     w10, restore_globals_after_bss                      //  Skip the loop if the bss section is zero length

loop_for_bss_clear:

    str     xzr, [x9], #8                                       //  Set 8 bytes at a time - the section in the linker script must match
    sub     w10, w10, #1
    cbnz    w10, loop_for_bss_clear

restore_globals_after_bss:
    //  BSS clear zeroed __hw_board_type and __number_of_cores_available (both live in BSS).
    //  Restore from preserved registers: x20 = board HW id, x24 = number of cores.
    //  CoreExecute() reads __number_of_cores_available directly — must be non-zero.

    SaveBoardHWType
    SaveNumberOfCoresAvailable

    //  Initialize the C++ statics
    //      Use registers x25 and x26 to hold the start and end addresses of the C++ statics to be initialized
    //      as the intializers may modify registers below 19.  Registers 19->28 are 'callee saved' registers
    //      so the called functions are required to preserve their values.
    //
    //  There is an array of function pointers that are called to initialize the C++ statics.  The array is
    //      defined in the linker script and the start and end addresses are passed to this code in the
    //      using symbols.  The code below loads the start and end addresses and loops over each - so long as
    //      there are > 0 initializers.

initialize_cpp_statics:

    adrp    x25, __init_array_start
    add     x25, x25, :lo12:__init_array_start
    adrp    x26, __init_array_end
    add     x26, x26, :lo12:__init_array_end
    cmp     x25, x26
    beq     start_additional_cores

loop_for_next_static_initializer:
    
    ldr     x0, [x25], 8                                        //  Load the next initializer function address into x0
    blr     x0                                                  //  Call the initializer function
    cmp     x25, x26
    bne     loop_for_next_static_initializer

    //  Bring the other cores out of their spin loops

start_additional_cores:

    mov x19, #1                                                 //  Start with core 1

start_next_core:
    mov x0, x19
    bl StartCore
    add x19, x19, #1
    cmp x19, x24_numberOfCoresAvailable                         //  Loop until all cores are started
    b.lo start_next_core

    //  Initialize the platform

    bl      InitializePlatform

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

    mov     x9, x0                                                  //  Copy the core number to a local register

    cmp     x23_arrivedInEL3, #1                                    //  If we arrived in EL3, then we do not need to release cores from the standard ARMSTUB
    b.eq    wait_core_ack

    mov     x10, #armstub_el2_spin_table					        //  Armstub EL2 spin table
	ldr     x11, =running_in_el2				                    //  Function pointer
	str	    x11, [x10, x9, lsl #3]				                    //  Store the function address to core to start
	sev										                        //  Wake up core

wait_core_ack:
	GetCoreState x9
	cmp	    w0, #CORE_SPINNING_IN_EL2			                    //  Wait for the core to be spinning in the startup code in EL2
	bne	    wait_core_ack

	ldr     x11, = configure_stacks_for_core	                    //  We want to configure the stacks next
	add	    x10, x22_coreJumpTableAddress, x9, lsl #3               //  Compute slot address so we can flush the cache line
	str	    x11, [x10]                                              //  Store the function address to the core
	dc 	    civac, x10                                              //  Flush to memory: secondary cores at EL2 have no D-cache and read directly from memory
	dsb	    sy                                                      //  Ensure flush completes before the event
	isb	    sy                                                      //  Instruction synchronization barrier
	sev										                        //  Wake up core

    ret                                                             //  Return

