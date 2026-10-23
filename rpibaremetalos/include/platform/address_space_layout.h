// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "os_config.h"
#include "os_memory_config.h"

//  TCR_EL1.T1SZ is 25 (TCREL1VAL in mmu.S): TTBR1_EL1 translates the top
//      2^(64-25) = 512GB of VA space, 0xFFFFFF80_00000000 upward.  The kernel is a
//      LINEAR map there -- kernel VA = physical + KERNEL_VA_BASE -- so converting is
//      an addition, never a lookup.
//
//  KERNEL_VA_BASE is 512GB-aligned.  The L1 index of a kernel virtual address
//      equals the L1 index of its physical address, so every identity-map table
//      this OS builds is ALSO a valid TTBR1 linear map, unchanged.

constexpr uint64_t KERNEL_VA_BASE = S_KERNEL_VA_BASE;

//  User address space (per task, TTBR0, T0SZ=25).
//
//  The window sits at 256GB (L1 index 256).  A task's L1 starts all zero, so nothing
//      requires it to sit that high; moving it lower is possible, but user.ld, hello.c and
//      the tests encode the address.

constexpr uint64_t USER_SPACE_BASE = 0x0000004000000000ULL;                                                         //  256GB, L1 index 256

//  The window is 64GB: it SPANS L1 SLOTS 256..319 ([256GB, 320GB)).  It is a virtual budget
//      for everything the task can address; with demand paging, untouched VA costs nothing.
//      Anything up to slot 511 (a 256GB window) needs only a new TOTAL_USER_SPACE_IN_MB.

constexpr uint64_t USER_SPACE_L1_INDEX_START = USER_SPACE_BASE >> 30;                                               //  256
constexpr uint64_t USER_SPACE_TOP            = USER_SPACE_BASE + ((uint64_t)TOTAL_USER_SPACE_IN_MB * BYTES_1M);     //  base + 64GB = 320GB
constexpr uint64_t USER_SPACE_L1_INDEX_END   = (USER_SPACE_TOP - 1) >> 30;                                          //  319

//  Regions, low to high.  image is fixed (the flat binary links here); heap grows UP to
//      heap_top; the guard gap is never mapped (a wild pointer faults instead of clobbering);
//      the stack grows DOWN from stack_top.

constexpr uint64_t USER_IMAGE_BASE   = USER_SPACE_BASE + ((uint64_t)USER_IMAGE_OFFSET_MB * BYTES_1M);               //  +4MB    header+text RO+X, then data+bss RW+XN
constexpr uint64_t USER_HEAP_BASE    = USER_SPACE_BASE + ((uint64_t)USER_HEAP_OFFSET_MB * BYTES_1M);                //  +256MB  RW+XN, grows up
constexpr uint64_t USER_STACK_TOP    = USER_SPACE_TOP;                                                              //  top of window
constexpr uint64_t USER_STACK_BOTTOM = USER_STACK_TOP - ((uint64_t)USER_MAX_STACK_SIZE_MB * BYTES_1M);              //  stack grows down to here
constexpr uint64_t USER_GUARD_BOTTOM = USER_STACK_BOTTOM - ((uint64_t)USER_GUARD_SIZE_MB * BYTES_1M);               //  [guard_bottom, stack_bottom) unmapped
constexpr uint64_t USER_HEAP_TOP     = USER_GUARD_BOTTOM;                                                           //  exclusive heap ceiling

static_assert(USER_IMAGE_BASE < USER_HEAP_BASE,        "image must sit below the heap");
static_assert(USER_HEAP_BASE < USER_HEAP_TOP,          "heap region must be non-empty");
static_assert(USER_HEAP_TOP == USER_GUARD_BOTTOM,      "heap ceiling abuts the guard");
static_assert(USER_GUARD_BOTTOM < USER_STACK_BOTTOM,   "guard gap must be non-empty");
static_assert(USER_STACK_BOTTOM < USER_STACK_TOP,      "stack region must be non-empty");
static_assert((USER_SPACE_BASE & (BYTES_1G - 1)) == 0, "user window base must be 1GB-aligned");
static_assert(USER_SPACE_TOP <= (1ULL << 39),          "user window must fit the T0SZ=25 512GB TTBR0 range");
static_assert(USER_SPACE_L1_INDEX_END >= USER_SPACE_L1_INDEX_START, "L1 slot range sanity");
static_assert(USER_SPACE_L1_INDEX_END < 512,           "window must end inside the 512-entry L1");

constexpr uint64_t PhysicalToKernelVirtualAddress(uint64_t physical_address) { return physical_address + KERNEL_VA_BASE; }
constexpr uint64_t KernelVirtualAddressToPhysical(uint64_t virtual_address)  { return virtual_address - KERNEL_VA_BASE; }
