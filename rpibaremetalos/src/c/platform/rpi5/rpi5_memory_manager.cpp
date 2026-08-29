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

    //  The VideoCore can only reach the low 1GB of physical RAM: BCM2712's
    //      dma-ranges map bus 0xC0000000-0xFFFFFFFF onto physical 0-0x40000000,
    //      which is exactly the aliasing ARMToGPUAddress()'s "| 0xC0000000"
    //      assumes.  RPi5 reports videocore memory at ~0xFDB00000 (~3.96GB), so
    //      the RPi3/RPi4 convention of placing these blocks just below videocore
    //      memory puts the DMA block at ~0xFD800000 -- an address that ALREADY
    //      has bits 30 and 31 set, making the OR a no-op.  The GPU then decodes
    //      that bus address as physical 0x3D800000, 0xC0000000 bytes from where
    //      the message was actually written, and every mailbox call fails.
    //
    //      Cap the reservation inside the GPU-addressable window instead,
    //      leaving the space above it in the low 1GB free for the firmware's own
    //      framebuffer allocations (1920x1080x32bpp is ~8MB; the framebuffer
    //      observed on this board landed at 0x3F800000).
    //
    //      NOTE: this also caps MemoryManager's allocatable RAM at ~896MB, since
    //      ReservedMemoryBase() is page_table_block_-derived.  Only the DMA block
    //      strictly needs to be GPU-addressable -- decoupling the page-table
    //      block so it can live high, and teaching MemoryManager to exclude the
    //      low DMA block, is a worthwhile follow-up on an 8GB board.

    const uint64_t gpu_addressable_top_block = (896 * BYTES_1M) / level1_blocksize_;

    const uint64_t reservation_top_block = minstd::min(videocore_memory_start_block_, gpu_addressable_top_block);

    dma_block_ = reservation_top_block - 1;
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

    //  Map the GPU allocation window uncached.
    //
    //      The firmware allocates the framebuffer inside the low-1GB
    //      GPU-addressable window (it landed at 0x3F800000 on this board).
    //      Unlike RPi3/RPi4 -- where the framebuffer falls inside the reported
    //      videocore range and is therefore already NORMAL_NO_CACHING -- RPi5
    //      reports videocore memory at ~0xFDB00000, nowhere near where the
    //      framebuffer actually lands.  Left as NORMAL cacheable, every pixel
    //      write sits in the D-cache where the display controller never sees
    //      it: the screen clears (an 8MB fill forces writeback) but no text
    //      ever appears (small glyph writes stay resident in cache).
    //
    //      This is exactly the region left free above our own reservation, so
    //      it does not overlap the page-table or DMA blocks.

    const uint64_t gpu_window_first_block = reservation_top_block;
    const uint64_t gpu_window_last_block  = BYTES_1G / level1_blocksize_;

    for (uint64_t i = gpu_window_first_block; i < gpu_window_last_block; i++)
    {
        Stage2map1to1_[i] = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::NORMAL_NO_CACHING,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
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