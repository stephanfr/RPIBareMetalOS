// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/memory_model.h"

#include "devices/log.h"

//  Sized for the largest subclass, exactly as MMUManager::Initialize() does for the board
//      memory managers.  MemoryModel::Initialize() runs before there is any heap.

union memory_model_union
{
    KernelOnly1To1MemoryModel kernel_only_1_to_1;
    KernelHighUserLowMemoryModel kernel_high_user_low;
};

uint8_t __memory_model_storage[sizeof(memory_model_union) + 16] __attribute__((aligned(16)));

void MemoryModel::Initialize(MemoryModelTypes type)
{
    if (instance_ != nullptr)
    {
        LogError("MemoryModel::Initialize() called more than once\n");
        return;
    }

    switch (type)
    {
        case MemoryModelTypes::KERNEL_ONLY_1_TO_1:
            instance_ = new ((void *)__memory_model_storage) KernelOnly1To1MemoryModel();
            break;

        case MemoryModelTypes::KERNEL_HIGH_USER_LOW:
            instance_ = new ((void *)__memory_model_storage) KernelHighUserLowMemoryModel();
            break;

        default:
            ParkCore();
    }

    //  Same cache-line treatment, and the same reason, as MMUManager::Initialize(): a
    //      secondary core may read through this with its D-cache off, so the writes have
    //      to reach DRAM rather than sit in core 0's cache.

    asm volatile("dc civac, %0" ::"r"(&instance_) : "memory");

    {
        const uint8_t *p = __memory_model_storage;
        const uint8_t *end = __memory_model_storage + sizeof(memory_model_union) + 16;

        for (; p < end; p += 64)
        {
            asm volatile("dc civac, %0" ::"r"(p) : "memory");
        }
    }

    asm volatile("dsb sy" ::: "memory");
}
