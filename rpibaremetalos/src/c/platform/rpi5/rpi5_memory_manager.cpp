// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi5/rpi5_memory_manager.h"

RPI5MemoryManager::RPI5MemoryManager(MemoryModelTypes memory_model)
    : AARCH64PlatformMemoryManager(memory_model)
{
    level1_blocksize_ = BYTES_1M * 2;   //  2MB
    level2_blocksize_ = BYTES_4K;       //  4KB granule
    granule_size_shift_ = 9;            //  Shift for 4k granule size

    //  Unlike RPi4, RPi5 has no peripherals below 4GB at all -- everything
    //  SoC-local lives at 0x10_xxxxxxxx+ (see the two direct L1 block writes
    //  below), so number_of_pagetable_entries_ only needs to cover actual
    //  installed RAM, not a low peripheral block. Still floor it at 4GB's
    //  worth of entries to match RPi3/RPi4's existing minimum-table-size
    //  convention.

    number_of_pagetable_entries_ = minstd::max( BYTES_1G * (uint64_t)4, platform_memory_in_bytes_ ) / (uint64_t)level1_blocksize_;
    uint32_t last_physical_memory_entry = platform_memory_in_bytes_ / level1_blocksize_;
    uint32_t entries_per_level1_block = (level1_blocksize_ / level2_blocksize_);

    videocore_memory_start_block_ = (uint64_t)videocore_memory_start_ / level1_blocksize_;

    //  Reserve a couple 'special purpose' blocks, same convention as RPi3/RPi4:
    //  one 2MB block for the page tables, one 2MB block of uncached memory for
    //  GPU-to-ARM communication (mailboxes/framebuffers), both placed just
    //  below Videocore memory.

    dma_block_ = videocore_memory_start_block_ - 1;
    page_table_block_ = dma_block_ - 1;

    kernel_page_table_1_to_1_ = (uint64_t *)(page_table_block_ * level1_blocksize_);
    Stage2map1to1_ = (VMSAv8_64_DESCRIPTOR *)(kernel_page_table_1_to_1_ + number_of_pagetable_entries_);

    //  Initialize the page tables to invalid

    for (uint64_t i = 0; i < number_of_pagetable_entries_; i++)
    {
        kernel_page_table_1_to_1_[i] = 0;
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){.Raw64 = 0};
    }

    //  RAM from 0x00 up to Videocore memory, as normal cacheable memory

    for (uint64_t i = 0; i < videocore_memory_start_block_; i++)
    {
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::NORMAL,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .SH = Stage2Sharability::INNER_SHAREABLE,
            .AF = AccessFlag::ACCESSED,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  Videocore memory itself, uncached

    for (uint64_t i = videocore_memory_start_block_; i < (videocore_memory_start_block_ + (videocore_memory_size_ / level1_blocksize_)); i++)
    {
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::NORMAL_NO_CACHING,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .AF = AccessFlag::ACCESSED,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  Remaining installed RAM above Videocore memory, normal cacheable again.
    //  (No low peripheral windows to skip around here, unlike RPi4 -- this
    //  loop simply runs to the end of installed RAM.)

    for (uint64_t i = (videocore_memory_start_block_ + (videocore_memory_size_ / level1_blocksize_)); i < last_physical_memory_entry; i++)
    {
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::NORMAL,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .SH = Stage2Sharability::INNER_SHAREABLE,
            .AF = AccessFlag::ACCESSED,
            .Address = (uintptr_t)i << granule_size_shift_,
        };
    }

    //  One block for non-cached DMA memory (mailbox/framebuffer traffic)

    Stage2map1to1_[dma_block_] = (VMSAv8_64_DESCRIPTOR){
        .EntryType = TableType::BLOCK_TABLE,
        .MemAttr = MemoryAttribute::NORMAL_NO_CACHING,
        .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
        .SH = Stage2Sharability::INNER_SHAREABLE,
        .AF = AccessFlag::ACCESSED,
        .Address = (uintptr_t)dma_block_ << granule_size_shift_,
    };

    //  Map the kernel 1:1 tables into the stage-2 map -- this only needs to
    //  cover actual installed RAM (a handful of entries), nowhere near the
    //  RPi5-specific indices (65, 124-127) written directly below.

    for (uint64_t i = 0; i < number_of_pagetable_entries_ / entries_per_level1_block; i++)
    {
        kernel_page_table_1_to_1_[i] = (0x8000000000000000) | (uintptr_t)&Stage2map1to1_[i * entries_per_level1_block] | 3;
    }

    //  RPi5-specific: two direct L1 1GB BLOCK descriptors, bypassing the
    //  Stage2map1to1_/2MB-block scheme entirely -- covering the ~1TiB gap
    //  between installed RAM and these high physical addresses at 2MB
    //  granularity would be both wasteful and wrong (it would mark hundreds
    //  of GB of nonexistent RAM as cacheable read-write memory). Mirrors the
    //  identical encoding mmu.S's SetupEarlyPageTables uses for the same two
    //  windows. Assumes installed RAM never reaches index 65 (i.e. < 32GB) --
    //  true for every currently-shipping RPi5 board (max 8GB).
    //
    //  Legacy peripheral/GIC/mailbox block: 0x10_7c000000-0x10_7fffffff, L1
    //  index 65 (this block's base is 0x10_40000000 -- the real peripherals
    //  sit partway into it, which is fine for a block descriptor; see the
    //  comment in mmu.S).
    //  RP1 PCIe outbound window: 0x1F_00000000-0x1F_FFFFFFFF (~4GB), L1
    //  indices 124-127.

    constexpr uint64_t RPI5_PERIPHERAL_L1_INDEX = 65;
    constexpr uint64_t RPI5_RP1_WINDOW_L1_START = 124;
    constexpr uint64_t RPI5_RP1_WINDOW_L1_END = 128;

    VMSAv8_64_DESCRIPTOR peripheral_block_descriptor = (VMSAv8_64_DESCRIPTOR){
        .EntryType = TableType::BLOCK_TABLE,
        .MemAttr = MemoryAttribute::DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
        .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
        .AF = AccessFlag::ACCESSED,
        .Address = RPI5_PERIPHERAL_L1_INDEX << 18,   //  (index << 30) >> 12, see Address field's bit offset
    };

    kernel_page_table_1_to_1_[RPI5_PERIPHERAL_L1_INDEX] = peripheral_block_descriptor.Raw64;

    for (uint64_t rp1_index = RPI5_RP1_WINDOW_L1_START; rp1_index < RPI5_RP1_WINDOW_L1_END; rp1_index++)
    {
        VMSAv8_64_DESCRIPTOR rp1_block_descriptor = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .AF = AccessFlag::ACCESSED,
            .Address = rp1_index << 18,
        };

        kernel_page_table_1_to_1_[rp1_index] = rp1_block_descriptor.Raw64;
    }
}