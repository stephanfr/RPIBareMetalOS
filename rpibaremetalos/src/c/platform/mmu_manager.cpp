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

void MMUManager::Initialize(MemoryModel memory_model)
{
    if(platform_memory_manager_ != nullptr)
    {
        LogError("MMUManager::Initialize() called more than once\n");
        return;
    }

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3 :
            platform_memory_manager_ = dynamic_cast<MMUManager*>(static_new<RPI3BPlusMemoryManager>(memory_model));
            break;

        case RPI_BOARD_ENUM_RPI4 :
            platform_memory_manager_ = dynamic_cast<MMUManager*>(static_new<RPI4BMemoryManager>(memory_model));
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
