// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "asm_globals.h"

#include "platform/address_space_layout.h"
#include "platform/memory_manager.h"

//  The kernel's one memory model: kernel high, user low, isolated.
//
//      TTBR1  the kernel, linked and running at KERNEL_VA_BASE, plus the linear map of RAM.
//      TTBR0  the current user task's own L1 (AddressSpace), or -- while a KERNEL task runs,
//             and at boot -- the permanently empty __empty_user_page_table, so any low VA
//             the kernel touches faults (R3).
//
//  A task's L1 starts all zero (AddressSpace::Initialize()): a task sees its own window and
//      nothing else.  Kernel VA<->physical conversion is NOT here; it stays in the inline
//      free functions in address_space_layout.h.
//
//  There is only this one model, so nothing here is virtual or selected at boot; the class
//      just keeps the policy in one place.

struct UserSpaceLayout
{
    uint64_t space_base;        //  first VA of the user window
    uint64_t space_top;         //  exclusive
    uint64_t l1_index_start;    //  space_base >> 30  (first L1 slot the window occupies)
    uint64_t l1_index_end;      //  (space_top - 1) >> 30  (last L1 slot, inclusive)
    uint64_t image_base;        //  header + text (fixed)
    uint64_t heap_base;         //  sc_Malloc grows up from here
    uint64_t heap_top;          //  exclusive ceiling for the heap (== guard_bottom)
    uint64_t guard_bottom;      //  [guard_bottom, stack_bottom) is never mapped
    uint64_t stack_bottom;      //  [stack_bottom, stack_top) is the user stack
    uint64_t stack_top;         //  exclusive; user stack grows down from here
};

class MemoryModel
{
public:
    MemoryModel() = delete;

    //  PHYSICAL base for TTBR0 at boot and whenever a KERNEL task is current.  Published as
    //      __boot_ttbr0_base by AARCH64PlatformMemoryManager::PublishBootPageTableBases().

    static uint64_t BootTTBR0Physical()
    {
        return KernelVirtualAddressToPhysical((uint64_t)&__empty_user_page_table);
    }

    //  One layout for every task: the user binary is a fixed-address flat image.

    static const UserSpaceLayout &UserSpace()
    {
        static const UserSpaceLayout layout
        {
            USER_SPACE_BASE,
            USER_SPACE_TOP,
            USER_SPACE_L1_INDEX_START,
            USER_SPACE_L1_INDEX_END,
            USER_IMAGE_BASE,
            USER_HEAP_BASE,
            USER_HEAP_TOP,
            USER_GUARD_BOTTOM,
            USER_STACK_BOTTOM,
            USER_STACK_TOP
        };

        return layout;
    }

    //  Every user frame -- task page tables, image blocks, heap and stack pages -- comes from
    //      here and goes back here, never from GetMemoryManager() directly.  A dedicated user
    //      pool or per-task accounting changes these two bodies and no call site.

    static MemoryPagePointer AllocateUserFrame(uint64_t size_in_bytes)
    {
        return GetMemoryManager().GetFreeBlock(size_in_bytes);
    }

    static void ReleaseUserFrame(MemoryPagePointer frame, uint64_t size_in_bytes)
    {
        GetMemoryManager().ReleaseBlock(frame, size_in_bytes);
    }
};
