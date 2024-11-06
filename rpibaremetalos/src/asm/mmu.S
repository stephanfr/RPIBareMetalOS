
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++}
{			MMU HELPER ROUTINES PROVIDE BY RPi-SmartStart API			    }
{++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
.equ MT_DEVICE_NGNRNE,	0
.equ MT_DEVICE_NGNRE,	1
.equ MT_DEVICE_GRE,		2
.equ MT_NORMAL_NC,		3
.equ MT_NORMAL,		    4
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

/* "PROVIDE C Function: void enable_mmu_tables ( RegType_t map1to1, RegType_t virtalmap);" */
.section .text.enable_mmu_tables, "ax", %progbits
.balign	4
.globl enable_mmu_tables;
.type enable_mmu_tables, %function
enable_mmu_tables:
	dsb sy

	/* Set the memattrs values into mair_el1*/
	ldr x2, =MAIR1VAL
    msr mair_el1, x2

	/* Bring both tables online and execute memory barrier */
	msr ttbr0_el1, x0
	msr ttbr1_el1, x1
	isb

	ldr x0, =TCREL1VAL
	msr tcr_el1, x0
	isb

	mrs x0, sctlr_el1
	ldr x1, =SCTLREL1VAL
	orr x0, x0, x1
	msr sctlr_el1, x0

	ret									
.balign	4
.ltorg										// Tell assembler ltorg data for this code can go here
.size	.enable_mmu_tables, .-enable_mmu_tables



/* "PROVIDE C FUNCTION: uint32_t ARMaddrToGPUaddr (void* ARMaddress);" */
.section .text.ARMaddrToGPUaddr, "ax", %progbits
.balign	4
.globl ARMaddrToGPUaddr;		
.type ARMaddrToGPUaddr, %function
ARMaddrToGPUaddr:
	mov x1, #0xC0000000						// ARM to VC conversion value
	orr x0, x0, x1							// Create bus address
	ret										// Return
.balign	4
.ltorg										// Tell assembler ltorg data for this code can go here
.size	ARMaddrToGPUaddr, .-ARMaddrToGPUaddr


/* "PROVIDE C FUNCTION: uint32_t GPUaddrToARMaddr (uint32_t BUSaddress);" */
.section .text.GPUaddrToARMaddr, "ax", %progbits
.balign	4
.globl GPUaddrToARMaddr;		
.type GPUaddrToARMaddr, %function
GPUaddrToARMaddr:
	mov x1, ~0xC0000000						// ARM to VC conversion value	
	and x0, x0, x1							// Create arm address
	ret										// Return
.balign	4
.ltorg										// Tell assembler ltorg data for this code can go here
.size	GPUaddrToARMaddr, .-GPUaddrToARMaddr




.global GetPhysicalAddress
GetPhysicalAddress:
	mov		x1, x0
	at		S1E1R, x0
	mrs		x0, PAR_EL1
	and		x0, x0, #0x000FFFFFFFFFF000
	and		x1, x1, #0x0000000000000FFF
	orr		x0, x0, x1
	ret