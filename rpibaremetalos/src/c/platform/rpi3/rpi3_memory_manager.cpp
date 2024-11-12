// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi3/rpi3_memory_manager.h"

extern "C" uint32_t GetVideocoreMemoryStart(uint32_t board_type);

RPI3BPlusMemoryManager::RPI3BPlusMemoryManager(MemoryModel memory_model)
    : AARCH64PlatformMemoryManager(memory_model)
{
    level1_blocksize_ = BYTES_1M * 2; //  2MB
    level2_blocksize_ = BYTES_4K;     //  4KB granule
    granule_size_shift_ = 9;          //  Shift for 4k granule size

    //  For the RPI3 B+, the model only has 1GB of memory, but the ARM peripherals are mapped into the first
    //      block after 1GB of memory.  Therefore, we will need a minimum of 1024 page table entries, but only
    //      the first 513 matter.

    number_of_pagetable_entries_ = 1024;

    videocore_memory_start_block_ = GetVideocoreMemoryStart(__hw_board_type) / level1_blocksize_;

    //  Reserve a couple 'special purpose' blocks.  First, a 2MB block to hold the page tables.  Second, a 2MB block
    //      of uncached memory for GPU to ARM communication (.e. mailboxes and frame buffers).  The 'dma_block' allows
    //      us to communicate with the GPU without having to put memory barriers everywhere in the code.
    //
    //  Put these two blocks just below the Videocore memory, this will be consistent between different Raspberry Pi models.

    dma_block_ = videocore_memory_start_block_ - 1;
    page_table_block_ = dma_block_ - 1;

    // 	Create page table blocks in the page table memory we set aside

    kernel_page_table_1_to_1_ = (uint64_t *)(page_table_block_ * level1_blocksize_);
    Stage2map1to1_ = (VMSAv8_64_DESCRIPTOR *)(kernel_page_table_1_to_1_ + number_of_pagetable_entries_);

    //  This is going to be inefficient but for setting up page tables, I'd rather be slow and
    //      correct than fast and wrong.

    //  Initialize the page tables to invalid

    for (uint64_t i = 0; i < number_of_pagetable_entries_; i++)
    {
        kernel_page_table_1_to_1_[i] = 0;
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){.Raw64 = 0};
    }

    //  Set the AccessFlag to ACCESSED for all entries in the page table.  If this flag is set to zero,
    //      the MMU will generate a fault on first access.

    // 	Initialize the memory from 0x00 up to the Videocore memory as normal, cacheable memory

    for (uint64_t i = 0; i < videocore_memory_start_block_; i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::NORMAL,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .SH = Stage2Sharability::INNER_SHAREABLE,
            .AF = AccessFlag::ACCESSED,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //	Videocore ram up to 0x3F000000

    for (uint64_t i = videocore_memory_start_block_; i < (0x3F000000 / level1_blocksize_); i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::NORMAL_NO_CACHING,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .AF = AccessFlag::ACCESSED,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  Main peripherals from 0x3F000000 - 0x40000000

    for (uint64_t i = (0x3F000000 / level1_blocksize_); i < (0x40000000 / level1_blocksize_); i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .AF = AccessFlag::ACCESSED,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  One 2MB block for ARM peripherals at 0x40000000

    Stage2map1to1_[(0x40000000 / level1_blocksize_)] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = TableType::BLOCK_TABLE,
        .MemAttr = MemoryAttribute::DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
        .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
        .AF = AccessFlag::ACCESSED,
        .Address = (uintptr_t)(0x40000000 / level1_blocksize_) << granule_size_shift_,
    };

    //	One block for non-cached DMA memory

    Stage2map1to1_[dma_block_] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = TableType::BLOCK_TABLE,
        .MemAttr = MemoryAttribute::NORMAL_NO_CACHING,
        .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
        .SH = Stage2Sharability::INNER_SHAREABLE,
        .AF = AccessFlag::ACCESSED,
        .Address = (uintptr_t)dma_block_ << granule_size_shift_,
    };

    // Level 1 has 2 valid entries mapping the each 1GB in stage2

    kernel_page_table_1_to_1_[0] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1_[0] | 3;
    kernel_page_table_1_to_1_[1] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1_[512] | 3;
}
