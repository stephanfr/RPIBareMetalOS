/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdint.h>
#include <heaps.h>
#include "asm_globals.h"
#include "cpu_part_nums.h"

#include "platform/kernel_command_line.h"

#include "platform/rpi4/rpi4_memory_manager.h"
#include "platform/rpi3/rpi3_memory_manager.h"

#include "devices/log.h"


union platform_specific_mmu_union
{
    RPI3BPlusMemoryManager rpi3;
    RPI4BMemoryManager rpi4;
};

uint8_t __mmu_manager_storage[sizeof(platform_specific_mmu_union) + 16] __attribute__((aligned(16)));


void MMUManager::Initialize()
{
    if(platform_memory_manager_ != nullptr)
    {
        LogError("MMUManager::Initialize() called more than once\n");
        return;
    }

    //  Get the memory model from the kernel command line

    MemoryModelTypes memory_model = MemoryModelTypes::KERNEL_ONLY_1_TO_1;

    minstd::fixed_string<64> memory_model_string;


    if( KernelCommandLine::FindSetting("memory_model", memory_model_string))
    {
        if( memory_model_string == KERNEL_ONLY_1_TO_1_STRING)
        {
            memory_model = MemoryModelTypes::KERNEL_ONLY_1_TO_1;
        }
        else
        {
            ParkCore();
        }
    }

    //  Create the platform specific memory manager
    //      Use placement new to create the object in the storage buffer allocaated above.

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI3BPlusMemoryManager(memory_model);
            break;

        case RPI_BOARD_ENUM_RPI4 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI4BMemoryManager(memory_model);
            break;

        default:
            ParkCore();
    }

    // Flush platform_memory_manager_ and the MMU manager object to DRAM.
    // Secondary cores start with D-cache disabled and read directly from DRAM in
    // EnableMMUForCore() — if these writes are still in core 0's L2 cache, the
    // secondary core sees null and parks.
    asm volatile("dc civac, %0" :: "r"(&platform_memory_manager_) : "memory");
    {
        const uint8_t *p = __mmu_manager_storage;
        const uint8_t *end = __mmu_manager_storage + sizeof(platform_specific_mmu_union) + 16;
        for (; p < end; p += 64)
        {
            asm volatile("dc civac, %0" :: "r"(p) : "memory");
        }
    }
    asm volatile("dsb sy" ::: "memory");

    // The MMU is already active (early tables from SetupEarlyPageTables/EnableMMUTables in start.S).
    // This call atomically swaps TTBR0 to the final platform-specific tables and invalidates
    // all TLB entries.  Safe because both early and final tables are 1:1 identity maps.
    platform_memory_manager_->EnableMMU();
}

extern "C" void EnableMMUForCore()
{
    MMUManager::Instance().EnableMMU();
}
