// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <platform/platform_mmu.h>

class RPI5MemoryManager : public AARCH64PlatformMemoryManager
{

    using VMSAv8_64_DESCRIPTOR = AARCH64PlatformMemoryManager::VMSAv8_64_DESCRIPTOR;
    using MemoryModelTypes = MMUManager::MemoryModelTypes;

public:
    RPI5MemoryManager(MemoryModelTypes memory_model);

    void EnableMMU() override
    {
        //  kernel_page_table_ is a kernel VA now.  PublishKernelPageTableBase converts
        //      it to physical internally, because a secondary core reads that global with
        //      its MMU off.

        PublishKernelPageTableBase((uint64_t)&kernel_page_table_[0]);

        //  Both arguments PHYSICAL (R1).  TTBR0 becomes model-decided in Step 1B.2.

        EnableMMUTables(KernelVirtualAddressToPhysical((uint64_t)&kernel_page_table_[0]),
                        KernelVirtualAddressToPhysical((uint64_t)&kernel_page_table_[0]));
    }

    void *DMAUncachedMemoryBase() const override
    {
        return (void *)PhysicalToKernelVirtualAddress(dma_block_ * level1_blocksize_);
    }

    void *ARMToGPUAddress(void *ARMaddress) const override
    {
        return (void *)(KernelVirtualAddressToPhysical((uintptr_t)ARMaddress) | 0xC0000000);
    }

private:

};