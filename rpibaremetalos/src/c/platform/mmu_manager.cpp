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

    platform_memory_manager_->EnableMMU();
}

extern "C" void EnableMMUForCore()
{
    MMUManager::Instance().EnableMMU();
}
