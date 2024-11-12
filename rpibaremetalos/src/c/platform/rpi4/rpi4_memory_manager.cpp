// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi4/rpi4_memory_manager.h"

#include <algorithm>

#include "asm_globals.h"

#include <minimalstdio.h>

RPI4BMemoryManager::RPI4BMemoryManager(MemoryModel memory_model)
    : AARCH64PlatformMemoryManager(memory_model)
{
    level1_blocksize_ = BYTES_1M * 2;   //  2MB
    level2_blocksize_ = BYTES_4K;       //  4KB granule
    granule_size_shift_ = 9;            //  Shift for 4k granule size

    //  For the RPI4, we will have a minimum of 2048 page table entries - even if we have only 1G of memory.
    //      In 'Low Peripheral Mode' the main peripherals and ARM Local peripherals are mapped into the top
    //      of a 4GB address space.

    number_of_pagetable_entries_ = minstd::max( BYTES_1G * (uint64_t)4, platform_memory_in_bytes_ ) / (uint64_t)level1_blocksize_;
    uint32_t last_physical_memory_entry = platform_memory_in_bytes_ / level1_blocksize_;
    uint32_t entries_per_level1_block = (level1_blocksize_ / level2_blocksize_);

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

    // 	Initialize the memory from 0x00 up to the Videocore memory as normal, cacheable memory

    for (uint64_t i = 0; i < videocore_memory_start_block_; i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MEMORY_ATTRIBUTE_NORMAL,
            .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
            .AF = 1,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //	Videocore ram up to 0x40000000

    for (uint64_t i = videocore_memory_start_block_; i < (0x40000000 / level1_blocksize_); i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MEMORY_ATTRIBUTE_NORMAL_NO_CACHING,
            .AF = 1,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  Normal memory again 0x40000000 to the end of the physical memory pagetable entries

    for (uint64_t i = (0x40000000 / level1_blocksize_); i < last_physical_memory_entry; i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MEMORY_ATTRIBUTE_NORMAL,
            .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
            .AF = 1,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  Main peripherals from 0xFC000000 - 0xFF800000

    for (uint64_t i = (0xFC000000 / level1_blocksize_); i < (0xFF800000 / level1_blocksize_); i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .AF = 1,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  ARM peripherals from 0xFF800000 - 0x100000000

    for (uint64_t i = (0xFF800000 / level1_blocksize_); i < (0x100000000 / level1_blocksize_); i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .AF = 1,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }


    // Finish the mapping to the end of memory

    for (uint64_t i = (0x100000000 / level1_blocksize_); i < last_physical_memory_entry; i++)
    {
        // Each block descriptor (2 MB)
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MEMORY_ATTRIBUTE_NORMAL,
            .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
            .AF = 1,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //	One block for non-cached DMA memory

    Stage2map1to1_[dma_block_] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = TableType::BLOCK_TABLE,
        .MemAttr = MEMORY_ATTRIBUTE_NORMAL_NO_CACHING,
        .SH = Stage2Sharability::STAGE2_SH_INNER_SHAREABLE,
        .AF = 1,
        .Address = (uintptr_t)dma_block_ << granule_size_shift_,
    };

    // Map the kernel 1 to 1 tables in the the stage 2 map

    for (uint64_t i = 0; i < number_of_pagetable_entries_ / entries_per_level1_block; i++)
    {
        kernel_page_table_1_to_1_[i] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1_[i * entries_per_level1_block] | 3;
    }
}
