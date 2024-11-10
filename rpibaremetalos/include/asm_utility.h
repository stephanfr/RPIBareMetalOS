// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

extern "C"
{
    void        EnableMMUTables ( uint64_t map1to1, uint64_t virtalmap);
    uint32_t    ARMaddrToGPUaddr (void* ARMaddress);
    void*       GPUaddrToARMaddr (uint32_t GPUaddress);
    void*       GetPhysicalAddress(void* virtual_address);

    void        CPUTicksDelay(uint64_t ticks);

    uint32_t    GetCoreID();
    void*       GetStackPointer();

    uint32_t    GetExceptionLevel();
    void*       GetTaskContext();

    bool        CoreExecute (uint32_t core, void (*func)(void));

    void        ParkCore();

    void        EnableIRQ(void);
    void        DisableIRQ(void);
}

#define INVALIDATE_CACHE_LINE(address)  asm volatile("dc cvau, %0" : : "r" (address) : "memory")
#define INSTRUCTION_CACHE_BARRIER       asm volatile ("isb sy")
#define SEND_EVENT                      asm volatile("sev")
#define WAIT_FOR_EVENT                  asm volatile("wfe")
#define WAIT_FOR_INTERRUPT              asm volatile("wfi")
