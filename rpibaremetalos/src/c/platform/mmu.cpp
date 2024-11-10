/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdint.h>
#include <heaps.h>
#include "asm_globals.h"
#include "cpu_part_nums.h"

#include "platform/rpi4/rpi4_memory_manager.h"
#include "platform/rpi3/rpi3_memory_manager.h"

#include "devices/log.h"

void MemoryManager::Initialize(MemoryModel memory_model)
{
    if(platform_memory_manager_ != nullptr)
    {
        LogError("MemoryManager::Initialize() called more than once\n");
        return;
    }

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3 :
            platform_memory_manager_ = dynamic_cast<MemoryManager*>(static_new<RPI3BPlusMemoryManager>(memory_model));
            break;

        case RPI_BOARD_ENUM_RPI4 :
            printf("RPI4 Memory Manager\n");
            platform_memory_manager_ = dynamic_cast<MemoryManager*>(static_new<RPI4BMemoryManager>(memory_model));
            break;

        default:
            printf("MemoryManager::Initialize() unknown board type\n");
            ParkCore();
    }

    platform_memory_manager_->EnableMMU();
}

extern "C" void EnableMMUForCore()
{
    MemoryManager::Instance().EnableMMU();
}
