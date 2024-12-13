// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

extern "C"
{
    void EnableMMUTables(uint64_t map1to1, uint64_t virtalmap);
    uint32_t ARMaddrToGPUaddr(void *ARMaddress);
    void *GPUaddrToARMaddr(uint32_t GPUaddress);
    void *GetPhysicalAddress(void *virtual_address);

    void CPUTicksDelay(uint64_t ticks);

    void ParkCore();
}

#define INVALIDATE_CACHE_LINE(address) asm volatile("dc cvau, %0" : : "r"(address) : "memory")
#define INSTRUCTION_CACHE_BARRIER asm volatile("isb sy")
#define SEND_EVENT asm volatile("sev")
#define WAIT_FOR_EVENT asm volatile("wfe")
#define WAIT_FOR_INTERRUPT asm volatile("wfi")

#define EnableIRQs() asm volatile("msr daifclr, #2")
#define DisableIRQs() asm volatile("msr daifset, #2")

//  Inline ASM functions

inline uint32_t GetCoreID()
{
    uint32_t core;

    asm volatile(
        "mrs %0, mpidr_el1\n\t"
        "and %0, %0, #3"
        : "=r"(core));

    return core;
}

inline void *GetTaskContext()
{
    void *task;

    //  The task context contains a pointer to the current task and is stored in both the TPIDR_EL1 and the TPIDRRO_EL0 registers.

    asm volatile(
        "mrs %0, tpidrro_el0"
        : "=r"(task));

    return task;
}

inline void *GetStackPointer()
{
    void *sp;
    asm volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

inline uint32_t GetExceptionLevel()
{
    uint32_t el;

    asm volatile(
        "mrs %0, CurrentEL\n\t"
        "lsr %0, %0, #2"
        : "=r"(el));

    return el;
}

