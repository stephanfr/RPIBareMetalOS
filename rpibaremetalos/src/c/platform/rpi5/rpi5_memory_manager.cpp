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

    //  Neither of RPi5's two "here is where VideoCore memory lives" reports can
    //      be trusted for this. GET_VC_MEMORY (the mailbox tag) answers
    //      ~0xFDB00000 (~3.96GB) -- a real firmware response, but one that
    //      already has bits 30/31 set, which makes ARMToGPUAddress()'s
    //      "| 0xC0000000" aliasing a no-op if anything is placed using it: the
    //      GPU would decode the resulting bus address as physical 0x3D800000,
    //      0xC0000000 bytes from where anything was actually written, and every
    //      mailbox call would fail. vc_mem.mem_base/vc_mem.mem_size on the
    //      kernel command line are not a safe substitute either -- they are an
    //      undocumented pass-through from firmware to the vc_mem Linux driver
    //      (drivers/char/broadcom/vc_mem.c declares both module_param()s with no
    //      MODULE_PARM_DESC), and mem_size is a suspiciously identical 1024 MiB
    //      on both RPi4 and RPi5 -- not a plausible gpu_mem= split, and more
    //      likely describing the ARM-visible GPU-addressable aperture itself
    //      than a live VideoCore RAM reservation. Neither is confirmed to answer
    //      the same question this code actually needs answered.
    //
    //      What IS confirmed, from primary source (BCM2712's dma-ranges: bus
    //      0xC0000000-0xFFFFFFFF maps to physical 0-0x40000000), is that the low
    //      1GB of physical RAM is the entire GPU-addressable window -- the same
    //      fact ARMToGPUAddress() already relies on below. Override the base
    //      class's mailbox-derived values with that sourced constant, so this is
    //      the intentional design rather than a mailbox-derived value that
    //      happens to always lose a min() against a hardcoded cap.

    videocore_memory_start_ = (uint8_t *)(896ULL * BYTES_1M);
    videocore_memory_size_ = (uint32_t)(BYTES_1G - (896ULL * BYTES_1M));

    videocore_memory_start_block_ = (uint64_t)videocore_memory_start_ / level1_blocksize_;

    //  The reservation sits just below the 896MB boundary above, leaving the
    //      remaining space up to 1GB free for the firmware's own framebuffer
    //      allocations (1920x1080x32bpp is ~8MB; the framebuffer observed on
    //      this board landed at 0x3F800000).
    //
    //      NOTE: this also caps MemoryManager's allocatable RAM at ~896MB, since
    //      ReservedMemoryBase() is page_table_block_-derived.  Only the DMA block
    //      strictly needs to be GPU-addressable -- decoupling the page-table
    //      block so it can live high, and teaching MemoryManager to exclude the
    //      low DMA block, is a worthwhile follow-up on an 8GB board.

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

    const uint64_t gpu_window_first_block = videocore_memory_start_block_;
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
    //  RPi5-specific indices (64-65, 124-127) written directly below.

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
    //  SoC-local peripheral windows, two direct 1GB device BLOCK descriptors:
    //    index 64 (0x10_00000000-0x10_3FFFFFFF): SD/EMMC (0x10_00FFF000), DMA, PCIe, USB.
    //    index 65 (0x10_40000000-0x10_7FFFFFFF): legacy peripheral/GIC/mailbox block
    //      (base 0x10_40000000; the real peripherals sit partway in, fine for a block).
    //  RP1 PCIe outbound window: 0x1F_00000000-0x1F_FFFFFFFF (~4GB), L1 indices 124-127.

    constexpr uint64_t RPI5_PERIPHERAL_L1_INDEX_FIRST = 64;
    constexpr uint64_t RPI5_PERIPHERAL_L1_INDEX_LAST = 65;
    constexpr uint64_t RPI5_RP1_WINDOW_L1_START = 124;
    constexpr uint64_t RPI5_RP1_WINDOW_L1_END = 128;

    for (uint64_t peripheral_index = RPI5_PERIPHERAL_L1_INDEX_FIRST; peripheral_index <= RPI5_PERIPHERAL_L1_INDEX_LAST; peripheral_index++)
    {
        VMSAv8_64_DESCRIPTOR peripheral_block_descriptor = (VMSAv8_64_DESCRIPTOR){
            .EntryType = TableType::BLOCK_TABLE,
            .MemAttr = MemoryAttribute::DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK,
            .S2AP = Stage2AccessPermission::EL1_READ_WRITE,
            .AF = AccessFlag::ACCESSED,
            .Address = peripheral_index << 18,   //  (index << 30) >> 12, see Address field's bit offset
        };

        kernel_page_table_1_to_1_[peripheral_index] = peripheral_block_descriptor.Raw64;
    }

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

    //  Add the standard reserved regions (kernel, stack, etc.) -- no GPU window to reserve on RPi5, since the GPU-addressable window is already reserved above.
    
    AddStandardReservedRegions();   //  BCM2712 has no peripherals below 4GB
}