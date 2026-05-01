//	The symbols below come from Leon de Boer's RPi-SmartStart project.
//		Only the MMU helper symbols and MMU enable code are used.

//"+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
//"																			"			
//"      Filename: smartstart64.s											"
//"      Copyright(c): Leon de Boer(LdB) 2017, 2018, 2019					"
//"      Version: 2.14														"
//"																			"		
//"***************[ THIS CODE IS FREEWARE UNDER CC Attribution]*************"
//"																			"
//"    This sourcecode is released for the purpose to promote programming   "
//"  on the Raspberry Pi. You may redistribute it and/or modify with the    "
//"  following disclaimer and condition.									"
//"																			"
//"      The SOURCE CODE is distributed "AS IS" WITHOUT WARRANTIES AS TO	"
//"   PERFORMANCE OF MERCHANTABILITY WHETHER EXPRESSED OR IMPLIED.			"
//"   Redistributions of source code must retain the copyright notices to	"
//"   maintain the author credit (attribution) .							"
//"																			"
//"*************************************************************************"

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++}
{			MMU HELPER ROUTINES PROVIDE BY RPi-SmartStart API			    }
{++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
.equ MT_DEVICE_NGNRNE,	0
.equ MT_DEVICE_NGNRE,	1
.equ MT_DEVICE_GRE,		2
.equ MT_NORMAL_NC,		3
.equ MT_NORMAL,		    4

// Early page table descriptor templates — indices must match MAIR1VAL byte slots above.
// NORMAL block: AttrIndx=4(MT_NORMAL), AP=EL1-RW(0), SH=InnerShareable(3), AF=1, BlockDesc(1)
.equ EARLY_NORMAL_DESC,    ((MT_NORMAL << 2) | (3 << 8) | (1 << 10) | 1)
// DEVICE block: AttrIndx=0(MT_DEVICE_NGNRNE), SH=NonShareable(0), AF=1, BlockDesc(1)
.equ EARLY_DEVICE_DESC,    ((MT_DEVICE_NGNRNE << 2) | (1 << 10) | 1)
// L1 table descriptor: NSTable=1 (bit 63, stored as top 16-bit word for movk), EntryType=TABLE(3)
.equ EARLY_L1_TABLE_FLAGS_HI, 0x8000
// L2 region boundaries (2MB block indices)
.equ EARLY_DEVICE_1_START, (0x3F000000 / 0x200000)   // 504:  RPi3 peripherals start
.equ EARLY_NORMAL_2_START, (0x40000000 / 0x200000)   // 512:  RAM above 1GB starts
.equ EARLY_DEVICE_2_START, (0xFC000000 / 0x200000)   // 2016: RPi4 peripherals start
.equ EARLY_L2_ENTRY_COUNT, (4 * 512)                 // 2048: covers 0-4GB
.equ MAIR1VAL, ( (0x00ul << (MT_DEVICE_NGNRNE * 8)) |\
                 (0x04ul << (MT_DEVICE_NGNRE * 8)) |\
				 (0x0cul << (MT_DEVICE_GRE * 8)) |\
                 (0x44ul << (MT_NORMAL_NC * 8)) |\
				 (0xfful << (MT_NORMAL * 8)) )

   // Specify mapping characteristics in translate control register
#define TCREL1VAL  ( (0b00LL << 37) |   /* TBI=0, no tagging */\
					 (0b000LL << 32) |  /* IPS= 32 bit ... 000 = 32bit, 001 = 36bit, 010 = 40bit */\
					 (0b10LL << 30)  |  /* TG1=4k ... options are 10=4KB, 01=16KB, 11=64KB ... take care differs from TG0 */\
					 (0b11LL << 28)  |  /* SH1=3 inner ... options 00 = Non-shareable, 01 = INVALID, 10 = Outer Shareable, 11 = Inner Shareable */\
					 (0b01LL << 26)  |  /* ORGN1=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b01LL << 24)  |  /* IRGN1=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b0LL  << 23)  |  /* EPD1 ... Translation table walk disable for translations using TTBR1_EL1  0 = walk, 1 = generate fault */\
					 (25LL   << 16)  |  /* T1SZ=25 (512G) ... The region size is 2 POWER (64-T1SZ) bytes */\
					 (0b00LL << 14)  |  /* TG0=4k  ... options are 00=4KB, 01=64KB, 10=16KB,  ... take care differs from TG1 */\
					 (0b11LL << 12)  |  /* SH0=3 inner ... .. options 00 = Non-shareable, 01 = INVALID, 10 = Outer Shareable, 11 = Inner Shareable */\
					 (0b01LL << 10)  |  /* ORGN0=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b01LL << 8)   |  /* IRGN0=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b0LL  << 7)   |  /* EPD0  ... Translation table walk disable for translations using TTBR0_EL1  0 = walk, 1 = generate fault */\
					 (25LL   << 0) ) 	/* T0SZ=25 (512G)  ... The region size is 2 POWER (64-T0SZ) bytes */
				     
#define SCTLREL1VAL ( (0xC00800) |		/* set mandatory reserved bits */\
					  (1 << 12)  |      /* I, Instruction cache enable. This is an enable bit for instruction caches at EL0 and EL1 */\
					  (1 << 4)   |		/* SA0, tack Alignment Check Enable for EL0 */\
					  (1 << 3)   |		/* SA, Stack Alignment Check Enable */\
					  (1 << 2)   |		/* C, Data cache enable. This is an enable bit for data caches at EL0 and EL1 */\
					  (1 << 1)   |		/* A, Alignment check enable bit */\
					  (1 << 0) )		/* set M, enable MMU */

.globl EnableMMUTables;
.align	4

EnableMMUTables:
	dsb     sy

	// Set memory attribute register
	ldr     x2, =MAIR1VAL
	msr     mair_el1, x2

	// Install translation table bases
	msr     ttbr0_el1, x0
	msr     ttbr1_el1, x1
	isb

	// Set translation control register
	ldr     x0, =TCREL1VAL
	msr     tcr_el1, x0
	isb

	// Invalidate all EL1 TLB entries before (re-)enabling the MMU.
	// Required by ARM ARM when changing TTBR or enabling translation.
	tlbi    vmalle1
	dsb     sy
	isb

	// Enable MMU, D-cache, and I-cache atomically
	mrs     x0, sctlr_el1
	ldr     x1, =SCTLREL1VAL
	orr     x0, x0, x1
	msr     sctlr_el1, x0
	isb                         // flush pipeline after MMU enable

	ret
.ltorg



//	uint32_t ARMaddrToGPUaddr (void* ARMaddress);
.align	4
.globl ARMaddrToGPUaddr;		
ARMaddrToGPUaddr:
	mov x1, #0xC0000000						//	ARM to VC conversion value
	orr x0, x0, x1							//	Create bus address
	ret
.ltorg


//	void* GPUaddrToARMaddr (uint32_t GPUaddress);
.align	4
.globl GPUaddrToARMaddr;		
GPUaddrToARMaddr:
	mov x1, ~0xC0000000						//	ARM to VC conversion value	
	and x0, x0, x1							//	Create arm address
	ret
.ltorg


//	uint64_t GetPhysicalAddress (uint64_t virtualAddress);
.align	4
.global GetPhysicalAddress
GetPhysicalAddress:
	mov		x1, x0
	at		S1E1R, x0
	mrs		x0, PAR_EL1
	and		x0, x0, #0x000FFFFFFFFFF000
	and		x1, x1, #0x0000000000000FFF
	orr		x0, x0, x1
	ret
.ltorg


// void SetupEarlyPageTables(void)
//
// Builds a conservative 1:1 identity-mapped page table covering 0-4GB before any
// C++ code runs.  Memory map (2MB blocks):
//   [0x000, 0x1F8): NORMAL  0x00000000-0x3EFFFFFF  (RAM, both boards)
//   [0x1F8, 0x200): DEVICE  0x3F000000-0x3FFFFFFF  (RPi3 peripherals)
//   [0x200, 0x7E0): NORMAL  0x40000000-0xFBFFFFFF  (RAM above 1GB on RPi4)
//   [0x7E0, 0x800): DEVICE  0xFC000000-0xFFFFFFFF  (RPi4 peripherals)
//
// Leaf function: uses x0-x6, preserves x19-x30.
//
.global SetupEarlyPageTables
.align 4
SetupEarlyPageTables:
    // Zero both tables (L1 + L2)
    adrp    x0, __early_l1_page_table
    add     x0, x0, :lo12:__early_l1_page_table
    adrp    x1, __early_page_tables_end
    add     x1, x1, :lo12:__early_page_tables_end
.Lzero_early_tables:
    str     xzr, [x0], #8
    cmp     x0, x1
    b.lo    .Lzero_early_tables

    // Build L2 entries: 2048 x 2MB blocks covering 0-4GB
    adrp    x0, __early_l2_page_table
    add     x0, x0, :lo12:__early_l2_page_table    // x0 = L2 table base
    mov     x3, #EARLY_NORMAL_DESC                  // x3 = NORMAL descriptor template
    mov     x4, #EARLY_DEVICE_DESC                  // x4 = DEVICE descriptor template
    mov     x5, #0                                  // x5 = entry index i
.Ll2_entry_loop:
    cmp     x5, #EARLY_DEVICE_1_START               // i < 0x1F8?
    b.lo    .Ll2_use_normal
    cmp     x5, #EARLY_NORMAL_2_START               // i < 0x200?
    b.lo    .Ll2_use_device
    cmp     x5, #EARLY_DEVICE_2_START               // i < 0x7E0?
    b.lo    .Ll2_use_normal
    b       .Ll2_use_device
.Ll2_use_normal:
    mov     x6, x3
    b       .Ll2_build_entry
.Ll2_use_device:
    mov     x6, x4
.Ll2_build_entry:
    orr     x6, x6, x5, lsl #21                    // insert physical address (i * 2MB) at bits[47:21]
    str     x6, [x0, x5, lsl #3]                   // store to L2[i]
    add     x5, x5, #1
    cmp     x5, #EARLY_L2_ENTRY_COUNT
    b.lo    .Ll2_entry_loop

    // Build 4 L1 entries pointing to consecutive 512-entry L2 sub-tables
    adrp    x0, __early_l1_page_table
    add     x0, x0, :lo12:__early_l1_page_table    // x0 = L1 table base
    adrp    x2, __early_l2_page_table
    add     x2, x2, :lo12:__early_l2_page_table    // x2 = first L2 sub-table address
    mov     x3, #3                                  // bits[1:0] = 0b11 (table descriptor)
    movk    x3, #EARLY_L1_TABLE_FLAGS_HI, lsl #48  // bit[63] = NSTable = 1
    mov     x4, #0                                  // x4 = L1 entry index
.Ll1_entry_loop:
    orr     x5, x3, x2                             // L1[i] = flags | L2 sub-table phys addr
    str     x5, [x0, x4, lsl #3]
    add     x2, x2, #4096                          // next sub-table (512 entries * 8 bytes)
    add     x4, x4, #1
    cmp     x4, #4
    b.lo    .Ll1_entry_loop

    dsb     sy                                      // ensure all table writes complete before caller enables MMU
    ret
.ltorg


