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

//  User address space (per task, TTBR0, T0SZ=25).  IDENTICAL under both memory models.
//
//  The window sits at 256GB rather than at a natural low base because under
//      kernel_only_1_to_1 a task's TTBR0 ALSO carries the kernel identity map, which
//      occupies physical RAM and MMIO at their own addresses -- a 4MB user image would
//      collide with the identity mapping of physical 4MB.  256GB is L1 index 256, which
//      is zero on every board (only slots 0-3, and RPi5's 64, 65, 124-127, are ever
//      written).  AddressSpace::Initialize() asserts that at runtime.
//
//  The whole 512MB window lies inside the single 1GB span of L1 index 256, so one L1
//      slot per task is all the user side ever consumes.

constexpr uint64_t USER_SPACE_BASE = 0x0000004000000000ULL;                                             //  256GB, L1 index 256
constexpr uint64_t USER_SPACE_L1_INDEX = USER_SPACE_BASE >> 30;                                         //  256

constexpr uint64_t USER_IMAGE_BASE = USER_SPACE_BASE + (4ULL * BYTES_1M);                               //  +4MB    header+text RO+X, then data+bss RW+XN
constexpr uint64_t USER_HEAP_BASE  = USER_SPACE_BASE + (256ULL * BYTES_1M);                             //  +256MB  RW+XN, grows up
constexpr uint64_t USER_SPACE_TOP  = USER_SPACE_BASE + ((uint64_t)TOTAL_USER_SPACE_IN_MB * BYTES_1M);   //  +512MB
constexpr uint64_t USER_STACK_TOP  = USER_SPACE_TOP;                                                    //          RW+XN, grows down

static_assert(USER_IMAGE_BASE < USER_HEAP_BASE && USER_HEAP_BASE < USER_STACK_TOP, "user layout order");
static_assert(USER_SPACE_TOP - USER_SPACE_BASE <= BYTES_1G, "user window must fit one L1 slot");
static_assert((USER_SPACE_BASE & (BYTES_1G - 1)) == 0, "user window must be 1GB-aligned");
static_assert(USER_SPACE_TOP < (1ULL << 39), "user window must fit the T0SZ=25 512GB TTBR0 range");

constexpr uint64_t PhysToKernelVA(uint64_t physical_address) { return physical_address + KERNEL_VA_BASE; }
constexpr uint64_t KernelVAToPhys(uint64_t virtual_address)  { return virtual_address - KERNEL_VA_BASE; }
