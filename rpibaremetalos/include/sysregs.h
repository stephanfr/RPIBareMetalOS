#pragma once

//  MIDR_EL1 Register

#define MIDR_EL1_IMPLEMENTER_OFFSET 24
#define MIDR_EL1_IMPLEMENTER_MASK (0xFF << MIDR_EL1_IMPLEMENTER_OFFSET)
#define MIDR_EL1_VARIANT_OFFSET 20
#define MIDR_EL1_VARIANT_MASK (0x0F << MIDR_EL1_VARIANT_OFFSET)
#define MIDR_EL1_ARCHITECTURE_OFFSET 16
#define MIDR_EL1_ARCHITECTURE_MASK (0x0F << MIDR_EL1_ARCHITECTURE_OFFSET)
#define MIDR_EL1_PARTNUM_OFFSET 4
#define MIDR_EL1_PARTNUM_MASK (0x0FFF << MIDR_EL1_PARTNUM_OFFSET)
#define MIDR_EL1_REVISION_OFFSET 0
#define MIDR_EL1_REVISION_MASK (0x0F << MIDR_EL1_REVISION_OFFSET)

// D13.2.113

#define SCTLR_RESERVED (3 << 28) | (3 << 22) | (1 << 20) | (1 << 11)
#define SCTLR_EE_LITTLE_ENDIAN (0 << 25)
#define SCTLR_EOE_LITTLE_ENDIAN (0 << 24)
#define SCTLR_I_CACHE_DISABLED (0 << 12)
#define SCTLR_D_CACHE_DISABLED (0 << 2)
#define SCTLR_MMU_DISABLED (0 << 0)
#define SCTLR_MMU_ENABLED (1 << 0)

#define SCTLR_VALUE_MMU_DISABLED (SCTLR_RESERVED | SCTLR_EE_LITTLE_ENDIAN | SCTLR_I_CACHE_DISABLED | SCTLR_D_CACHE_DISABLED | SCTLR_MMU_DISABLED)

// D13.2.47

#define HCR_RW (1 << 31)
#define HCR_VALUE HCR_RW

// D13.2.112

#define SCR_RESERVED (3 << 4)
#define SCR_RW (1 << 10)
#define SCR_NS (1 << 0)
#define SCR_VALUE (SCR_RESERVED | SCR_RW | SCR_NS)

// C5.2.19

#define SPSR_MASK_ALL (7 << 6)
#define SPSR_EL1h (5 << 0)
#define SPSR_EL2h (9 << 0)
#define SPSR_VALUE (SPSR_MASK_ALL | SPSR_EL1h)

//  Exception Syndrome Register

#define ESR_ELx_EC_SHIFT		26
#define ESR_ELx_EC_MASK			0x3F    //  EC[5:0], after shifting right by ESR_ELx_EC_SHIFT
#define ESR_ELx_EC_SVC64		0x15    //  SVC instruction executed in AArch64 state
#define ESR_ELx_EC_IABT_LOW		0x20    //  Instruction Abort from a LOWER exception level (EL0 -> EL1)
#define ESR_ELx_EC_DABT_LOW		0x24    //  Data Abort from a LOWER exception level (EL0 -> EL1)
#define ESR_ELx_EC_DABT_CUR		0x25    //  Data Abort taken without a change in exception level (EL1 -> EL1)

//  Data Abort ISS fields (EC == DABT_*).
//
//  DFSC = ISS[5:0].  0b0001LL (0x04-0x07) is "translation fault, level LL" -- the signature
//      of an unmapped page, which is exactly what demand paging recovers from.  NOTE 0x00-0x03
//      are ADDRESS SIZE faults, not translation faults.  Anything else (access flag 0x08-0x0B,
//      permission 0x0C-0x0F, external abort, alignment) is a real fault and kills the task.
//  FnV = ISS[10]: when set, FAR_EL1 is not valid and the fault cannot be resolved.

#define ESR_ELx_DFSC_MASK		0x3F
#define ESR_ELx_DFSC_IS_TRANSLATION(dfsc)	(((dfsc) & 0x3C) == 0x04)
#define ESR_ELx_FnV				(1UL << 10)

//  PSR bits

#define PSR_MODE_EL0t 0x00000000
#define PSR_MODE_EL1t 0x00000004
#define PSR_MODE_EL1h 0x00000005
#define PSR_MODE_EL2t 0x00000008
#define PSR_MODE_EL2h 0x00000009
#define PSR_MODE_EL3t 0x0000000c
#define PSR_MODE_EL3h 0x0000000d

