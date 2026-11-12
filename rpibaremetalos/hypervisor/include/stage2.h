// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Stage-2 translation tables: 4 KB granule, 39-bit IPA, walk starting at level 1 (VTCR_EL2.T0SZ = 25, SL0 = 1).
//      Pure C over a caller-supplied pool of 4 KB pages, so the host unit tests can compile it.
//
//  Mapping picks the largest entry alignment allows: 1 GB at level 1, 2 MB at level 2, 4 KB at level 3.  Changing
//      part of a block splits it, and the new table inherits the block's mapping.  An unmapped entry keeps a
//      STAGE2_REASON_* in its software bits, for the fault report.
//
//  Edits are not break-before-make: a table that a core is already walking must not be changed through this API.

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define STAGE2_PAGE_SIZE 0x1000ull
#define STAGE2_IPA_BITS 39
#define STAGE2_IPA_LIMIT (1ull << STAGE2_IPA_BITS)

//  Leaf attributes: everything in a block or page descriptor except its type and output address.

#define STAGE2_MEMATTR_DEVICE_NGNRE (0x1ull << 2)
#define STAGE2_MEMATTR_NORMAL_WB (0xFull << 2) //  outer and inner write-back
#define STAGE2_MEMATTR_MASK (0xFull << 2)
#define STAGE2_S2AP_READ (1ull << 6)
#define STAGE2_S2AP_WRITE (1ull << 7)
#define STAGE2_SH_INNER (3ull << 8)
#define STAGE2_AF (1ull << 10)
#define STAGE2_XN (1ull << 54)

#define STAGE2_ATTRIBUTES_NORMAL (STAGE2_MEMATTR_NORMAL_WB | STAGE2_S2AP_READ | STAGE2_S2AP_WRITE | STAGE2_SH_INNER | STAGE2_AF)
#define STAGE2_ATTRIBUTES_DEVICE (STAGE2_MEMATTR_DEVICE_NGNRE | STAGE2_S2AP_READ | STAGE2_S2AP_WRITE | STAGE2_AF | STAGE2_XN)
#define STAGE2_ATTRIBUTES_DEVICE_READ_ONLY (STAGE2_ATTRIBUTES_DEVICE & ~STAGE2_S2AP_WRITE)

//  Why an IPA is unmapped.  NONE: nothing was ever mapped there.  FIRMWARE: the EL3 code below the hypervisor.
//      VIDEOCORE: the VideoCore firmware's own memory.

#define STAGE2_REASON_NONE 0
#define STAGE2_REASON_HYPERVISOR 1
#define STAGE2_REASON_POOL 2
#define STAGE2_REASON_BLOCKED 3
#define STAGE2_REASON_MEDIATED 4
#define STAGE2_REASON_FIRMWARE 5
#define STAGE2_REASON_VIDEOCORE 6
#define STAGE2_REASON_MAX STAGE2_REASON_VIDEOCORE

#define STAGE2_SUCCESS 0
#define STAGE2_ERROR_ALIGNMENT 1
#define STAGE2_ERROR_RANGE 2
#define STAGE2_ERROR_NO_MEMORY 3
#define STAGE2_ERROR_NOT_INITIALIZED 4

    //  Lives in HypervisorState, so its fields are plain integers.  'pointer_offset' is added to a physical address
    //      to reach it: 0 at EL2, where the MMU is off; the host tests use it to place the pool in their own memory.

    typedef struct Stage2Tables
    {
        uint64_t root; //  PA of the level-1 table, the value for VTTBR_EL2.BADDR; 0 until Stage2Init
        uint64_t pool_base;
        uint64_t pointer_offset;
        uint32_t pool_pages;
        uint32_t pool_used;
    } Stage2Tables;

    typedef struct Stage2Translation
    {
        uint64_t physical_address;
        uint64_t attributes; //  STAGE2_ATTRIBUTES_* bits of the leaf; 0 when unmapped
        uint32_t level;      //  of the leaf or of the invalid entry that ended the walk
        uint32_t reason;     //  STAGE2_REASON_*, when unmapped
    } Stage2Translation;

    //  Takes over 'pool_pages' pages at 'pool_base' (4 KB aligned) and allocates an empty root from them.  Anything
    //      previously built in the pool is forgotten.

    uint32_t Stage2Init(volatile Stage2Tables *tables, uint64_t pool_base, uint32_t pool_pages, uint64_t pointer_offset);

    //  IPA [ipa, ipa + size) to PA [pa, pa + size), replacing whatever was there.  All three 4 KB aligned.  On
    //      STAGE2_ERROR_NO_MEMORY part of the range may already be changed.

    uint32_t Stage2Map(volatile Stage2Tables *tables, uint64_t ipa, uint64_t pa, uint64_t size, uint64_t attributes);

    uint32_t Stage2Unmap(volatile Stage2Tables *tables, uint64_t ipa, uint64_t size, uint32_t reason);

    //  Returns 1 and fills 'translation' when 'ipa' is mapped; 0, with the level and reason filled, when not.

    uint32_t Stage2Translate(const volatile Stage2Tables *tables, uint64_t ipa, Stage2Translation *translation);

    //  1 when every page of [ipa, ipa + size) is mapped identity (PA == IPA) with exactly 'attributes'.

    uint32_t Stage2RangeIsIdentity(const volatile Stage2Tables *tables, uint64_t ipa, uint64_t size,
                                   uint64_t attributes);

    //  Never null.

    const char *Stage2ReasonName(uint32_t reason);

#ifdef __cplusplus
}
#endif
