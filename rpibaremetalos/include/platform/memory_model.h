// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "asm_globals.h"

#include "platform/address_space_layout.h"
#include "platform/memory_manager.h"
#include "platform/mmu_manager.h"

//  The kernel is linked and runs at KERNEL_VA_BASE in EVERY model -- that is decided by
//      link.template.ld, not by policy -- so kernel VA<->physical conversion is NOT here.
//      It stays as the inline free functions in address_space_layout.h.
//
//  Constructed exactly once, by MMUManager::Initialize(), from the kernel command line,
//      BEFORE the board memory manager is constructed -- board page-table builders may
//      consult it.  Placement-new into a static buffer; there is no heap that early.

struct UserSpaceLayout
{
    uint64_t space_base;        //  first VA of the user window
    uint64_t space_top;         //  exclusive
    uint64_t l1_index;          //  space_base >> 30
    uint64_t image_base;        //  header + text
    uint64_t heap_base;         //  sc_Malloc grows up from here
    uint64_t stack_top;         //  user stack grows down from here
};

//  One shared layout: the user binary is a fixed-address flat image, so it is not
//      relocatable and cannot have a different base per model.

inline const UserSpaceLayout &SharedUserSpaceLayout()
{
    static const UserSpaceLayout layout{
        USER_SPACE_BASE,
        USER_SPACE_TOP,
        USER_SPACE_L1_INDEX,
        USER_IMAGE_BASE,
        USER_HEAP_BASE,
        USER_STACK_TOP};

    return layout;
}

//  A 4KB L1 holds 512 entries, which is the whole T0SZ=25 range.

constexpr uint32_t TASK_L1_ENTRY_COUNT = 512;

class MemoryModel
{
public:
    using MemoryModelTypes = MMUManager::MemoryModelTypes;

    virtual ~MemoryModel() = default;

    virtual MemoryModelTypes Type() const = 0;
    virtual const char *Name() const = 0;

    //  ---- Tier 1: TTBR0 policy ----

    //  PHYSICAL base for TTBR0 at boot and whenever a KERNEL task is current.  Takes the
    //      kernel L1 as a kernel VA; models that do not want it simply ignore it.

    virtual uint64_t BootTTBR0Physical(const uint64_t *kernel_l1_va) const = 0;

    //  ---- Tier 1: per-task tables (used from Phase 2) ----

    //  Initialise a freshly allocated, 512-entry task L1.  Returns false to refuse.  This
    //      is the ONLY place a model decides what a user task can see besides its own window.

    virtual bool SeedTaskL1(uint64_t *task_l1_va) const = 0;

    //  ---- Tier 1: layout ----

    virtual const UserSpaceLayout &UserSpace() const = 0;

    //  ---- Tier 1: user-frame hook (Tier 3 replaces the BODY, never the call sites) ----

    virtual MemoryPagePointer AllocateUserFrame(uint64_t size_in_bytes) const
    {
        return GetMemoryManager().GetFreeBlock(size_in_bytes);
    }

    virtual void ReleaseUserFrame(MemoryPagePointer frame, uint64_t size_in_bytes) const
    {
        GetMemoryManager().ReleaseBlock(frame, size_in_bytes);
    }

    //  ---- Construction and access ----

    static void Initialize(MemoryModelTypes type);      //  placement-new the right subclass

    static const MemoryModel &Instance()
    {
        if (instance_ == nullptr)
        {
            ParkCore();
        }

        return *instance_;
    }

    static bool IsInitialized() { return instance_ != nullptr; }

protected:
    static inline MemoryModel *instance_ = nullptr;
};

//  The bring-up/debug model.  TTBR0 keeps the kernel identity map, so a physical address
//      the kernel forgot to convert is still dereferenceable.

class KernelOnly1To1MemoryModel : public MemoryModel
{
public:
    MemoryModelTypes Type() const override { return MemoryModelTypes::KERNEL_ONLY_1_TO_1; }
    const char *Name() const override { return MMUManager::KERNEL_ONLY_1_TO_1_STRING; }

    uint64_t BootTTBR0Physical(const uint64_t *kernel_l1_va) const override
    {
        return KernelVirtualAddressToPhysical((uint64_t)kernel_l1_va);
    }

    bool SeedTaskL1(uint64_t *task_l1_va) const override
    {
        //  This task's TTBR0 carries the kernel identity map too, so a syscall or an interrupt
        //      taken while this task is current still resolves a physical address.  Copied
        //      VERBATIM -- S2AP stays EL1_READ_WRITE, so EL0 still cannot reach kernel memory
        //      (see Step 2.1).
        //
        //  Only the L1 is copied.  Every task therefore SHARES the kernel's L2/L3 tables for
        //      kernel memory, which is both correct and free: nothing rewrites the kernel tables
        //      after the board constructor builds them.  If that ever changes, these copies go
        //      stale and this is the code to revisit.

        const uint64_t *kernel_l1 = MMUManager::Instance().KernelPageTableL1();

        for (uint32_t i = 0; i < TASK_L1_ENTRY_COUNT; i++)   //  4KB L1 / 8 = 512, the whole T0SZ=25 range
        {
            task_l1_va[i] = kernel_l1[i];
        }

        return true;
    }

    const UserSpaceLayout &UserSpace() const override { return SharedUserSpaceLayout(); }
};
