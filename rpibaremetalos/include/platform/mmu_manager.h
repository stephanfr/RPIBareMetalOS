// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "os_entity.h"

#include "asm_utility.h"

extern "C" void EnableMMUForCore();

class MMUManager
{
public:
    typedef enum class MemoryModelTypes : uint32_t
    {
        KERNEL_ONLY_1_TO_1,
    } MemoryModelTypes;

    static constexpr const char* KERNEL_ONLY_1_TO_1_STRING = "kernel_only_1_to_1";

    static void Initialize();

    static MMUManager &Instance()
    {
        if (platform_memory_manager_ == nullptr)
        {
            ParkCore();
        }

        return *platform_memory_manager_;
    }

    virtual void *DMAUncachedMemoryBase() const = 0;

    virtual void *ARMToGPUAddress(void *ARMaddress) const = 0;

    virtual MemoryModelTypes MemoryModel() const = 0;

    static bool IsMMUEnabled()
    {
        return (platform_memory_manager_ != nullptr);
    }

private:
    friend void EnableMMUForCore();

    static inline MMUManager *platform_memory_manager_ = nullptr;

    virtual void EnableMMU() = 0;
};


inline const char *ToString(MMUManager::MemoryModelTypes model)
{
    switch(  model )
    {
        case MMUManager::MemoryModelTypes::KERNEL_ONLY_1_TO_1:
            return MMUManager::KERNEL_ONLY_1_TO_1_STRING;
    }
}
