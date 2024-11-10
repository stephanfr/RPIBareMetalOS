/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdint.h>
#include <heaps.h>
#include "asm_globals.h"
#include "cpu_part_nums.h"

#include "platform/rpi4/rpi4_memory_manager.h"
#include "platform/rpi3/rpi3_memory_manager.h"

#include "devices/log.h"

//#include "devices/mailbox_messages.h"

//#include "asm_globals.h"
//#include "asm_utility.h"
//#include "cpu_part_nums.h"


//extern "C" void enable_mmu_tables(uint64_t map1to1, uint64_t virtalmap);


/*

void *__dma_block_base;
uint32_t __dma_block_size;

void *__page_table_block_base;
uint32_t __page_table_block_size;

bool __mmu_enabled = false;

// We have 2Mb blocks, so we need 2 of 512 entries
// Covers 2GB which is enuf for the 1GB + QA7 we need
#define NUM_PAGE_TABLE_ENTRIES 2048
// Each Block is 2Mb in size
#define LEVEL1_BLOCKSIZE (1 << 21)
// LEVEL1 TABLE ALIGNMENT 4K
#define TLB_ALIGNMENT 4096
// LEVEL2 TABLE ALIGNMENT 4K
#define TLB2_ALIGNMENT 4096

//					   PRIVATE INTERNAL MEMORY DATA

// First Level Page Table for 1:1 mapping
static uint64_t __attribute__((aligned(TLB_ALIGNMENT))) page_table_map1to1[NUM_PAGE_TABLE_ENTRIES] = {0};
// First Level Page Table for virtual mapping
static uint64_t __attribute__((aligned(TLB_ALIGNMENT))) page_table_virtualmap[NUM_PAGE_TABLE_ENTRIES] = {0};
// First Level Page Table for virtual mapping
static uint64_t __attribute__((aligned(TLB2_ALIGNMENT))) Stage2virtual[2048] = {0};

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

        // These are only valid on BLOCK DESCRIPTOR
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


//					 CODE TYPE STRUCTURE COMPILE TIME CHECKS
//
// If you have never seen compile time assertions it's worth google search
// on "Compile Time Assertions". It is part of the C11++ specification and
// all compilers that support the standard will have them (GCC, MSC inc)


// Check the code type structure size
static_assert(sizeof(VMSAv8_64_DESCRIPTOR) == sizeof(uint64_t), "VMSAv8_64_DESCRIPTOR should be size of a register");

// Level 2 and final ... 1 to 1 mapping
// This will have 2048 entries x 2M so a full range of 2GB
static VMSAv8_64_DESCRIPTOR __attribute__((aligned(TLB_ALIGNMENT))) Stage2map1to1[2048] = {0};

// Stage3 ... Virtual mapping stage3 (final) ... basic minimum of a single table
static __attribute__((aligned(TLB_ALIGNMENT))) VMSAv8_64_DESCRIPTOR Stage3virtual[512] = {0};

//-[ MMU_setup_pagetable ]--------------------------------------------------}
//  Sets up a default TLB table. This needs to be called by only once by one
//  core on a multicore system. Each core can use the same default table.
//--------------------------------------------------------------------------
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

    //  Reserve a couple 'special purpose' blocks.  First, a 2MB block to hold the page tables.  Second, a 2MB block
    //      of uncached memory for GPU to ARM communication (.e. mailboxes and frame buffers).  The 'dma_block' allows
    //      us to communicate with the GPU without having to put memory barriers everywhere in the code.
    //
    //  Put these two blocks just below the Videocore memory, this will be consistent between different Raspberry Pi models.

    uint32_t dma_block = minstd::min(last_block, videocore_base_address_in_blocks) - 1;
    uint32_t page_table_block = dma_block - 1;

    __uncached_memory_base = (void *)(dma_block * LEVEL1_BLOCKSIZE);

    printf("DMA block: %d, uncached memory base: %p\n", dma_block, __uncached_memory_base);

    // 	Initialize 1:1 mapping for TTBR0
    // 		The 21-12 entries are because that is only for 4K granual it makes it obvious to change for other granual sizes

    // 	Initialize the memory from 0x00 up to the Videocore memory as normal, cacheable memory

    for (base = 0; base < dma_block; base++)
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

    base = dma_block;

    Stage2map1to1[base] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = 1,
        .MemAttr = MEMORY_ATTRIBUTE_NORMAL_NO_CACHING,
        .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
        .AF = 1,
        .Address = (uintptr_t)base << (21 - 12),
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

    //  Normal memory again from 0x3F000000 to 0xFE000000

    for (; base < 2048 - 16; base++)
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

    // 30 MB peripherals at 0xFE000000 - 0xFF800000
    for (; base < 2048; base++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1[base] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = 1,
            .MemAttr = MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .AF = 1,
            .Address = (uintptr_t)base << (21 - 12),
        };
    }

    // 2 MB for mailboxes at 0xFF800000
    // shared device, never execute
//    Stage2map1to1[2047] = (VMSAv8_64_DESCRIPTOR){
//        .EntryType = 1,
//        .MemAttr = MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
//        .AF = 1,
//        .Address = (uintptr_t)512 << (21 - 12),
//    };

    // Level 1 has 4 valid entries mapping the each 1GB in stage2 to cover the 4GB
    page_table_map1to1[0] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1[0] | 3;
    page_table_map1to1[1] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1[512] | 3;
    page_table_map1to1[2] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1[1024] | 3;
    page_table_map1to1[3] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1[1536] | 3;

    // Initialize virtual mapping for TTBR1 .. basic 1 page  .. 512 entries x 4K pages
    // 2MB of ram memory memory  0xFFFFFFFFFFE00000 to 0xFFFFFFFFFFFFFFFF

    // Stage2 virtual has just 1 valid entry (the last) of the 512 entries pointing to the Stage3 virtual table
    // Stage 3 starts as all invalid they will be added by mapping call
    // Stage2virtual[511] = (VMSAv8_64_DESCRIPTOR){ .NSTable = 1,.Address = (uintptr_t)& Stage3virtual[0] >> 12,.EntryType = 3 };
    Stage2virtual[2047] = (0x8000000000000000) | (uintptr_t)&Stage3virtual[0] | 3;

    // Stage1 virtual has just 1 valid entry (the last) of 512 entries pointing to the Stage2 virtual table
    page_table_virtualmap[2047] = (0x8000000000000000) | (uintptr_t)&Stage2virtual[0] | 3;
}

//-[ MMU_enable ]-----------------------------------------------------------}
//  Enables the MMU system to the previously created TLB tables.
//--------------------------------------------------------------------------
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

*/

void MemoryManager::Initialize(MemoryModel memory_model)
{
    if(platform_memory_manager_ != nullptr)
    {
        LogError("MemoryManager::Initialize() called more than once\n");
        return;
    }

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3 :
            platform_memory_manager_ = dynamic_cast<MemoryManager*>(static_new<RPI3BPlusMemoryManager>(memory_model));
            break;

        case RPI_BOARD_ENUM_RPI4 :
            printf("RPI4 Memory Manager\n");
            platform_memory_manager_ = dynamic_cast<MemoryManager*>(static_new<RPI4BMemoryManager>(memory_model));
            break;

        default:
            printf("MemoryManager::Initialize() unknown board type\n");
            ParkCore();
    }

    platform_memory_manager_->EnableMMU();
}

extern "C" void EnableMMUForCore()
{
    MemoryManager::Instance().EnableMMU();
}
