// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  VM 1's physical memory: each board's RAM limit and device windows, the checks on what the kernel hands EL2, and
//      the stage-2 map built from them.  Pure C, so the host unit tests can compile it.  Each int64_t result is
//      HVC_SUCCESS or the HVC result code to report.

#include <stdint.h>

#include "hypervisor_config.h"
#include "stage2.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HYPERVISOR_VMID 1

//  The pool: stage-2 tables from the bottom, then one MAILBOX_CALL bounce buffer per core at the top.

#define HYPERVISOR_POOL_MAILBOX_SIZE (MAX_CORES * HYPERVISOR_MAILBOX_MESSAGE_MAX)
#define HYPERVISOR_POOL_TABLES_SIZE (HYPERVISOR_POOL_SIZE - HYPERVISOR_POOL_MAILBOX_SIZE)

    uint64_t HypervisorMailboxBuffer(uint64_t pool_base, uint32_t core);

    //  The VideoCore mailbox's registers (read, status, write), or 0 for an unknown board.

    uint64_t HypervisorMailboxRegisters(uint32_t board);

    //  MEMORY_DONATE's range: exactly HYPERVISOR_POOL_SIZE, aligned to it, in RAM, and clear of the hypervisor's
    //      region and the device windows.

    int64_t HypervisorDonationCheck(uint32_t board, uint64_t physical_address, uint64_t size);

    //  STAGE2_ENABLE's RAM size: 2 MB granular, at most the board's largest, and past the hypervisor's region.

    int64_t HypervisorRamSizeCheck(uint32_t board, uint64_t ram_size);

    //  Pages EL2 emulates for VM 1 (hypervisor_mmio.h): unmapped at stage-2, so every access traps.

#define HYPERVISOR_DEVICE_MMIO_TEST 1
#define HYPERVISOR_DEVICE_MAILBOX_PAGE 2 //  the page holding the mailbox registers
#define HYPERVISOR_DEVICE_SDHCI 3        //  an SD host controller, which could otherwise DMA anywhere

    typedef struct HypervisorMediatedDevice
    {
        uint64_t base; //  IPA, 4 KB aligned
        uint64_t size;
        uint32_t kind; //  HYPERVISOR_DEVICE_*
    } HypervisorMediatedDevice;

    //  The board's mediated devices, by index from 0.  Returns 0 past the last.

    uint32_t HypervisorMediatedDeviceAt(uint32_t board, uint64_t pool_base, uint32_t index,
                                        HypervisorMediatedDevice *device);

    //  The mediated device whose range holds 'ipa'.  Returns 0 when there is none.

    uint32_t HypervisorMediatedDeviceFor(uint32_t board, uint64_t pool_base, uint64_t ipa,
                                         HypervisorMediatedDevice *device);

    //  The VideoCore firmware's memory, which VM 1 must not reach: the VideoCore can write anywhere, so a kernel able
    //      to rewrite its code could have it write EL2.  The framebuffer inside it stays mapped.  A size of 0 means
    //      the memory is unknown, and it stays mapped.

    typedef struct HypervisorVideoCoreMemory
    {
        uint64_t base;
        uint64_t size;
        uint64_t framebuffer_base;
        uint64_t framebuffer_size;
    } HypervisorVideoCoreMemory;

    //  GET_VC_MEMORY's answer, with the bus-alias bits dropped, must be page aligned, inside the first 1 GB and RAM,
    //      and clear of the hypervisor's region and the pool; otherwise '*memory' is all 0.  The framebuffer is rounded
    //      out to pages and kept only when it lies wholly inside.

    int64_t HypervisorVideoCoreMemoryCheck(uint64_t ram_size, uint64_t pool_base, uint32_t reply_base,
                                           uint32_t reply_size, uint64_t framebuffer_base, uint64_t framebuffer_size,
                                           HypervisorVideoCoreMemory *memory);

    //  VM 1's identity map, in the donated pool: RAM [0, ram_size) as Normal write-back, the board's device windows
    //      blocked over it except for the allowed device pages, Device-nGnRE, and the EL3 firmware, the hypervisor's
    //      region, the pool, the VideoCore's memory and the mediated devices unmapped.  The pool must lie in RAM.

    int64_t HypervisorStage2Build(volatile Stage2Tables *tables, uint32_t board, uint64_t ram_size, uint64_t pool_base,
                                  const HypervisorVideoCoreMemory *videocore, uint64_t pointer_offset);

    //  LOCKDOWN's table edit: the board's lockdown ranges, plus [named_base, named_base + named_size) when the size
    //      isn't 0, become read-only.  The named range must lie in the board's region for it, and every page must be
    //      an allowed device page; otherwise nothing changes.  Allocates nothing.  The caller invalidates the TLBs.

    int64_t HypervisorLockdownTables(volatile Stage2Tables *tables, uint32_t board, uint64_t named_base, uint64_t named_size);

    //  VTCR_EL2 for those tables, or 0 when ID_AA64MMFR0_EL1 says the core can't address them.

    uint64_t HypervisorVtcr(uint64_t id_aa64mmfr0);

    uint64_t HypervisorVttbr(uint64_t root);

#ifdef __cplusplus
}
#endif
