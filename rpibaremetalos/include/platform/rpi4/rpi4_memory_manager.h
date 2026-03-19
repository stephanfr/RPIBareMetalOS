// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <platform/platform_mmu.h>

class RPI4BMemoryManager : public AARCH64PlatformMemoryManager
{

    using VMSAv8_64_DESCRIPTOR = AARCH64PlatformMemoryManager::VMSAv8_64_DESCRIPTOR;
    using MemoryModelTypes = MMUManager::MemoryModelTypes;

public:
    RPI4BMemoryManager(MemoryModelTypes memory_model);

    void EnableMMU() override
    {
        EnableMMUTables((uint64_t)&kernel_page_table_1_to_1_[0], (uint64_t)0);
    }

    void *DMAUncachedMemoryBase() const override
    {
        return (void *)(dma_block_ * level1_blocksize_);
    }

    void *ARMToGPUAddress(void *ARMaddress) const override
    {
        return (void *)((uintptr_t)ARMaddress | 0xC0000000);
    }

private:

};
