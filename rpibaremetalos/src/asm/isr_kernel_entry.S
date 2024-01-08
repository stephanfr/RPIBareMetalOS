

#define SYNC_INVALID_EL1t		0 
#define IRQ_INVALID_EL1t		1 
#define FIQ_INVALID_EL1t		2 
#define ERROR_INVALID_EL1t		3 

#define SYNC_INVALID_EL1h		4 
#define IRQ_INVALID_EL1h		5 
#define FIQ_INVALID_EL1h		6 
#define ERROR_INVALID_EL1h		7 

#define SYNC_INVALID_EL0_64	    8 
#define IRQ_INVALID_EL0_64	   	9 
#define FIQ_INVALID_EL0_64		10 
#define ERROR_INVALID_EL0_64	11 

#define SYNC_INVALID_EL0_32		12 
#define IRQ_INVALID_EL0_32		13 
#define FIQ_INVALID_EL0_32		14 
#define ERROR_INVALID_EL0_32	15 


//
//  Pretty simple IRQ kernel entry and exit routines below.  On entry, save all the registers on the stack.
//      On exit, restore the registers and execute the exception return.
//
//  The entry and exit routines optimize performance by reserving a block on the stack and then writing 
//      the CPU registers into that block in pairs (rather than push and pop).  Still we are saving a
//      bunch of registers, this is not super-lightweight....
//

#define S_FRAME_SIZE			256 		//  Space needed on stack to save all the registers 

.macro	kernel_entry
    sub	sp, sp, #S_FRAME_SIZE
    stp	x0, x1, [sp, #16 * 0]
    stp	x2, x3, [sp, #16 * 1]
    stp	x4, x5, [sp, #16 * 2]
    stp	x6, x7, [sp, #16 * 3]
    stp	x8, x9, [sp, #16 * 4]
    stp	x10, x11, [sp, #16 * 5]
    stp	x12, x13, [sp, #16 * 6]
    stp	x14, x15, [sp, #16 * 7]
    stp	x16, x17, [sp, #16 * 8]
    stp	x18, x19, [sp, #16 * 9]
    stp	x20, x21, [sp, #16 * 10]
    stp	x22, x23, [sp, #16 * 11]
    stp	x24, x25, [sp, #16 * 12]
    stp	x26, x27, [sp, #16 * 13]
    stp	x28, x29, [sp, #16 * 14]
    str	x30, [sp, #16 * 15] 
.endm

.macro	kernel_exit
    ldp	x0, x1, [sp, #16 * 0]
    ldp	x2, x3, [sp, #16 * 1]
    ldp	x4, x5, [sp, #16 * 2]
    ldp	x6, x7, [sp, #16 * 3]
    ldp	x8, x9, [sp, #16 * 4]
    ldp	x10, x11, [sp, #16 * 5]
    ldp	x12, x13, [sp, #16 * 6]
    ldp	x14, x15, [sp, #16 * 7]
    ldp	x16, x17, [sp, #16 * 8]
    ldp	x18, x19, [sp, #16 * 9]
    ldp	x20, x21, [sp, #16 * 10]
    ldp	x22, x23, [sp, #16 * 11]
    ldp	x24, x25, [sp, #16 * 12]
    ldp	x26, x27, [sp, #16 * 13]
    ldp	x28, x29, [sp, #16 * 14]
    ldr	x30, [sp, #16 * 15] 
    add	sp, sp, #S_FRAME_SIZE		
    eret
.endm



.macro handle_invalid_entry type
    kernel_entry

    //  Setup the call to show the invalid entry message

    mov	x0, #\type
    mrs	x1, esr_el1
    mrs	x2, elr_el1
    bl	show_invalid_entry_message
    
    //  Just park the core, no recovery possible

    b ParkCore

.endm

//
//  Macro to define an exception vector table entry.  Interrupts are a subset of Exceptions in AARCH64. 
//      
//  Each vector table entry may be a maximum of 128 bytes (0x80) so the alignment for each
//      table entry is specified as 7 which results in 2^7 or 128 bytes per table entry.
//      
//  Each entry simply takes a label and places a 'branch to' label instruction into the table.
//

.macro	exception_vector_table_entry	label
    .align	7
    b	\label
.endm


.align	11
.globl _exception_vectors
_exception_vectors:

    //  This first set of four entries is for exceptions raised when execution is at EL1 and the SPSel register holds 0.
    //      When SPSel = 0, the stack pointer (SP) is shared between EL1 and EL0. 

	exception_vector_table_entry	sync_invalid_el1t			// Synchronous EL1t
	exception_vector_table_entry	irq_invalid_el1t            // IRQ EL1t
	exception_vector_table_entry	fiq_invalid_el1t			// FIQ EL1t
	exception_vector_table_entry	error_invalid_el1t			// Error EL1t

    //  This second set of four entries is for exceptions raised when execution is at EL1 and the SPSel register holds 1.
    //      When SPSel = 1, there are separate stack pointers (SP) for EL1 and EL0 (and EL2 and EL3 also).

	exception_vector_table_entry	sync_invalid_el1h			// Synchronous EL1h
	exception_vector_table_entry	el1_irq		                // IRQ EL1h
	exception_vector_table_entry	fiq_invalid_el1h			// FIQ EL1h
	exception_vector_table_entry	error_invalid_el1h			// Error EL1h

    //  This first set of four entries is for exceptions raised when execution is at EL0 and the core is executing 64 bit code

	exception_vector_table_entry	sync_invalid_el0_64			// Synchronous 64-bit EL0
	exception_vector_table_entry	irq_invalid_el0_64			// IRQ 64-bit EL0
	exception_vector_table_entry	fiq_invalid_el0_64			// FIQ 64-bit EL0
	exception_vector_table_entry	error_invalid_el0_64		// Error 64-bit EL0

    //  This first set of four entries is for exceptions raised when execution is at EL0 and the core is executing 32 bit code

	exception_vector_table_entry	sync_invalid_el0_32			// Synchronous 32-bit EL0
	exception_vector_table_entry	irq_invalid_el0_32			// IRQ 32-bit EL0
	exception_vector_table_entry	fiq_invalid_el0_32			// FIQ 32-bit EL0
	exception_vector_table_entry	error_invalid_el0_32		// Error 32-bit EL0

//
//  Branch symbols for the exception vector table entries
//      Error conditions are fully enumerated.
//

sync_invalid_el1t:
	handle_invalid_entry  SYNC_INVALID_EL1t

irq_invalid_el1t:
	handle_invalid_entry  IRQ_INVALID_EL1t

fiq_invalid_el1t:
	handle_invalid_entry  FIQ_INVALID_EL1t

error_invalid_el1t:
	handle_invalid_entry  ERROR_INVALID_EL1t

sync_invalid_el1h:
	handle_invalid_entry  SYNC_INVALID_EL1h

irq_invalid_el1h:
	handle_invalid_entry  IRQ_INVALID_EL1h

fiq_invalid_el1h:
	handle_invalid_entry  FIQ_INVALID_EL1h

error_invalid_el1h:
	handle_invalid_entry  ERROR_INVALID_EL1h

sync_invalid_el0_64:
	handle_invalid_entry  SYNC_INVALID_EL0_64

irq_invalid_el0_64:
	handle_invalid_entry  IRQ_INVALID_EL0_64

fiq_invalid_el0_64:
	handle_invalid_entry  FIQ_INVALID_EL0_64

error_invalid_el0_64:
	handle_invalid_entry  ERROR_INVALID_EL0_64

sync_invalid_el0_32:
	handle_invalid_entry  SYNC_INVALID_EL0_32

irq_invalid_el0_32:
	handle_invalid_entry  IRQ_INVALID_EL0_32

fiq_invalid_el0_32:
	handle_invalid_entry  FIQ_INVALID_EL0_32

error_invalid_el0_32:
	handle_invalid_entry  ERROR_INVALID_EL0_32

el1_irq:
	kernel_entry 
	bl	handle_irq
	kernel_exit 

.globl err_hang
err_hang: b err_hang
