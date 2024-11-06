/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdint.h>

#include "platform/mmu.h"

#include "devices/mailbox_messages.h"

#include "asm_utility.h"

extern "C" void enable_mmu_tables(uint64_t map1to1, uint64_t virtalmap);

extern void *__uncached_memory_base;

/* We have 2Mb blocks, so we need 2 of 512 entries	*/
/* Covers 2GB which is enuf for the 1GB + QA7 we need */
#define NUM_PAGE_TABLE_ENTRIES 512
/* Each Block is 2Mb in size */
#define LEVEL1_BLOCKSIZE (1 << 21)
/* LEVEL1 TABLE ALIGNMENT 4K */
#define TLB_ALIGNMENT 4096
/* LEVEL2 TABLE ALIGNMENT 4K */
#define TLB2_ALIGNMENT 4096

/***************************************************************************}
{						   PRIVATE INTERNAL MEMORY DATA				        }
****************************************************************************/
/* First Level Page Table for 1:1 mapping */
static uint64_t __attribute__((aligned(TLB_ALIGNMENT))) page_table_map1to1[NUM_PAGE_TABLE_ENTRIES] = {0};
/* First Level Page Table for virtual mapping */
static uint64_t __attribute__((aligned(TLB_ALIGNMENT))) page_table_virtualmap[NUM_PAGE_TABLE_ENTRIES] = {0};
/* First Level Page Table for virtual mapping */
static uint64_t __attribute__((aligned(TLB2_ALIGNMENT))) Stage2virtual[512] = {0};

typedef enum Stage2AccessPermission : uint32_t
{
    STAGE2_S2AP_NOREAD_EL0 = 1, //			No read access for EL0
    STAGE2_S2AP_NO_WRITE = 2,   //			No write access
} Stage2AccessPermission;

typedef enum Stage2Sharability : uint32_t
{
    STAGE2_SH_OUTER_SHAREABLE = 2, //			Outer shareable
    STAGE2_SH_INNER_SHAREABLE = 3, //			Inner shareable
} Stage2Sharability;

typedef union
{
    struct
    {
        uint64_t EntryType : 2; // @0-1		1 for a block table, 3 for a page table

        /* These are only valid on BLOCK DESCRIPTOR */
        uint64_t MemAttr : 4;            // @2-5
        Stage2AccessPermission S2AP : 2; // @6-7
        Stage2Sharability SH : 2;        // @8-9
        uint64_t AF : 1;                 // @10		Accessable flag

        uint64_t _reserved11 : 1;    // @11		Set to 0
        uint64_t Address : 36;       // @12-47	36 Bits of address
        uint64_t _reserved48_51 : 4; // @48-51	Set to 0
        uint64_t Contiguous : 1;     // @52		Contiguous
        uint64_t _reserved53 : 1;    // @53		Set to 0
        uint64_t XN : 1;             // @54		No execute if bit set
        uint64_t _reserved55_58 : 4; // @55-58	Set to 0

        uint64_t PXNTable : 1; // @59      Never allow execution from a lower EL level
        uint64_t XNTable : 1;  // @60		Never allow translation from a lower EL level
                               //		enum {
                               //			APTABLE_NOEFFECT = 0,			// No effect
                               //			APTABLE_NO_EL0 = 1,				// Access at EL0 not permitted, regardless of permissions in subsequent levels of lookup
                               //			APTABLE_NO_WRITE = 2,			// Write access not permitted, at any Exception level, regardless of permissions in subsequent levels of lookup
                               //			APTABLE_NO_WRITE_EL0_READ = 3	// Write access not permitted,at any Exception level, Read access not permitted at EL0.
                               //		}
        uint64_t APTable : 2;  // @61-62	AP Table control .. see enumerate options
        uint64_t NSTable : 1;  // @63		Secure state, for accesses from Non-secure state this bit is RES0 and is ignored
    };
    uint64_t Raw64; // @0-63	Raw access to all 64 bits via this union
} VMSAv8_64_DESCRIPTOR;

/*--------------------------------------------------------------------------}
{					 CODE TYPE STRUCTURE COMPILE TIME CHECKS	            }
{--------------------------------------------------------------------------*/
/* If you have never seen compile time assertions it's worth google search */
/* on "Compile Time Assertions". It is part of the C11++ specification and */
/* all compilers that support the standard will have them (GCC, MSC inc)   */
/*-------------------------------------------------------------------------*/

/* Check the code type structure size */
static_assert(sizeof(VMSAv8_64_DESCRIPTOR) == sizeof(uint64_t), "VMSAv8_64_DESCRIPTOR should be size of a register");

/* Level 2 and final ... 1 to 1 mapping */
/* This will have 1024 entries x 2M so a full range of 2GB */
static VMSAv8_64_DESCRIPTOR __attribute__((aligned(TLB_ALIGNMENT))) Stage2map1to1[1024] = {0};

/* Stage3 ... Virtual mapping stage3 (final) ... basic minimum of a single table */
static __attribute__((aligned(TLB_ALIGNMENT))) VMSAv8_64_DESCRIPTOR Stage3virtual[512] = {0};

/*-[ MMU_setup_pagetable ]--------------------------------------------------}
.  Sets up a default TLB table. This needs to be called by only once by one
.  core on a multicore system. Each core can use the same default table.
.--------------------------------------------------------------------------*/
void MMU_setup_pagetable(void)
{
    uint32_t base;

    // 	Get the base address and size of the Videocore memory.  This requires going to
    //		the mailbox system to get the information.

    Mailbox mbox(GetPlatformInfo().GetMMIOBase());

    GetVideocoreMemoryTag getVideocoreMemoryTag;

    MailboxPropertyMessage getVideocoreMemoryMessage(getVideocoreMemoryTag);

    mbox.sendMessage(getVideocoreMemoryMessage);

    printf("Videocore Memory Base Address: %p\n", (void *)getVideocoreMemoryTag.GetBaseAddress());
    printf("Videocore Memory Size: %d\n", getVideocoreMemoryTag.GetSizeInBytes());

    uint32_t videocore_base_address_in_blocks = getVideocoreMemoryTag.GetBaseAddress() / LEVEL1_BLOCKSIZE;

    uint32_t last_block = GetPlatformInfo().GetMemorySizeInBytes() / LEVEL1_BLOCKSIZE;

    printf("VC4 base address in blocks: %d, last block: %d\n", videocore_base_address_in_blocks, last_block);

    //	Reserve one block for uncached normal memory.  We will us this block for memory that is shared with
    //		the GPU.  If we fon't do this, then we have to add memory barriers throughout the code to
    //		insure cache coherency when there is a conversation between an ARM core and the GPU.
    //
    //	FWIW - I could not get the memory barriers to work correctly anyway.

    uint32_t dma_block = minstd::min(last_block, videocore_base_address_in_blocks) - 1;

    __uncached_memory_base = (void *)(dma_block * LEVEL1_BLOCKSIZE);

    printf("DMA block: %d, uncached memory base: %p\n", dma_block, __uncached_memory_base);

    // 	Initialize 1:1 mapping for TTBR0
    // 		The 21-12 entries are because that is only for 4K granual it makes it obvious to change for other granual sizes

    // 	Initialize the memory from 0x00 up to the Videocore memory as normal, cacheable memory

    for (base = 0; base < videocore_base_address_in_blocks; base++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1[base] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = 1,
            .MemAttr = MEMORY_ATTRIBUTE_NORMAL,
            .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
            .AF = 1,
            .Address = (uintptr_t)base << (21 - 12),
        };
    }

    //	One block for non-cached DMA memory

    Stage2map1to1[dma_block] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = 1,
        .MemAttr = MEMORY_ATTRIBUTE_NORMAL_NO_CACHING,
        .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
        .AF = 1,
        .Address = (uintptr_t)dma_block << (21 - 12),
    };

    //	Videocore ram up to 0x3F000000

    for (; base < 512 - 8; base++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1[base] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = 1,
            .MemAttr = MEMORY_ATTRIBUTE_NORMAL_NO_CACHING,
            .AF = 1,
            .Address = (uintptr_t)base << (21 - 12),
        };
    }

    // 16 MB peripherals at 0x3F000000 - 0x40000000
    for (; base < 512; base++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1[base] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = 1,
            .MemAttr = MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .AF = 1,
            .Address = (uintptr_t)base << (21 - 12),
        };
    }

    // 2 MB for mailboxes at 0x40000000
    // shared device, never execute
    Stage2map1to1[512] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = 1,
        .MemAttr = MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
        .AF = 1,
        .Address = (uintptr_t)512 << (21 - 12),
    };

    // Level 1 has just 2 valid entries mapping the each 1GB in stage2 to cover the 2GB
    page_table_map1to1[0] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1[0] | 3;
    page_table_map1to1[1] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1[512] | 3;

    // Initialize virtual mapping for TTBR1 .. basic 1 page  .. 512 entries x 4K pages
    // 2MB of ram memory memory  0xFFFFFFFFFFE00000 to 0xFFFFFFFFFFFFFFFF

    // Stage2 virtual has just 1 valid entry (the last) of the 512 entries pointing to the Stage3 virtual table
    // Stage 3 starts as all invalid they will be added by mapping call
    // Stage2virtual[511] = (VMSAv8_64_DESCRIPTOR){ .NSTable = 1,.Address = (uintptr_t)& Stage3virtual[0] >> 12,.EntryType = 3 };
    Stage2virtual[511] = (0x8000000000000000) | (uintptr_t)&Stage3virtual[0] | 3;

    // Stage1 virtual has just 1 valid entry (the last) of 512 entries pointing to the Stage2 virtual table
    page_table_virtualmap[511] = (0x8000000000000000) | (uintptr_t)&Stage2virtual[0] | 3;
}

/*-[ MMU_enable ]-----------------------------------------------------------}
.  Enables the MMU system to the previously created TLB tables.
.--------------------------------------------------------------------------*/
extern "C" void MMU_enable(void)
{
    enable_mmu_tables((uint64_t)&page_table_map1to1[0], (uint64_t)&page_table_virtualmap[0]);
}

uint64_t virtualmap(uint32_t phys_addr, uint8_t memattrs)
{
    uint64_t addr = 0;
    for (int i = 0; i < 512; i++)
    {
        if (Stage3virtual[i].Raw64 == 0)
        { // Find the first vacant stage3 table slot
            uint64_t offset;
            Stage3virtual[i] = (VMSAv8_64_DESCRIPTOR){.EntryType = 3, .MemAttr = memattrs, .AF = 1, .Address = (uintptr_t)phys_addr << (21 - 12)};
            __asm volatile("dmb sy" ::: "memory");
            offset = ((512 - i) * 4096) - 1;
            addr = 0xFFFFFFFFFFFFFFFFul;
            addr = addr - offset;
            return (addr);
        }
    }
    return (addr); // error
}
