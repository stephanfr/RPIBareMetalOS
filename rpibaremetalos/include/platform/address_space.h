// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <array>

#include "asm_globals.h"

#include "platform/address_space_layout.h"
#include "platform/memory_manager.h"
#include "platform/memory_model.h"
#include "platform/platform_mmu.h"

class AddressSpace
{
public:
    static constexpr uint16_t KERNEL_ASID = 0;                  //  the empty table; never a task's

    struct OwnedBlock
    {
        uint64_t physical;
        uint64_t size;
    };

    static constexpr uint32_t MAX_OWNED_BLOCKS = 512;           //  ~8KB of bookkeeping per task

    AddressSpace() = default;
    ~AddressSpace();                                            //  releases every OwnedBlock

    AddressSpace(const AddressSpace &) = delete;
    AddressSpace &operator=(const AddressSpace &) = delete;

    //  Allocate the L1, seed it per the memory model, take an ASID.

    bool Initialize();

    bool MapPages(uint64_t user_va, uint64_t physical_base, uint64_t size,
                  Stage2AccessPermission ap, bool executable);

    //  Software walk of THIS space's tables.  Returns false if user_va is unmapped OR
    //      outside [USER_SPACE_BASE, USER_SPACE_TOP).  This is how the kernel reads user
    //      memory without ever using a user VA itself.
    //
    //  The range check is what makes the syscall boundary MODEL-INDEPENDENT: under
    //      kernel_only_1_to_1 this table also contains the kernel identity map, and
    //      without the check a user pointer of 0x80000 would translate happily and hand
    //      the kernel its own image.  Range-check FIRST, walk second, always.

    bool Translate(uint64_t user_va, uint64_t &physical_out) const;

    uint64_t TTBR0Value() const { return ((uint64_t)asid_ << 48) | l1_physical_; }   //  ASID [63:48], PHYSICAL L1 [47:0]

    //  What TTBR0 holds while a KERNEL task runs.  This is the whole of the runtime
    //      difference between the two memory models; Step 1B.2b already published it as
    //      __boot_ttbr0_base, so just return that.

    static uint64_t KernelTTBR0Value() { return __boot_ttbr0_base; }                  //  ASID 0

private:
    static constexpr uint32_t ENTRIES_PER_TABLE = 512;
    static constexpr uint64_t PAGE_SIZE = BYTES_4K;

    uint64_t *l1_ = nullptr;                                    //  kernel VA
    uint64_t l1_physical_ = 0;
    uint16_t asid_ = KERNEL_ASID;

    minstd::array<OwnedBlock, MAX_OWNED_BLOCKS> owned_;         //  table frames and mapped blocks alike
    uint32_t owned_count_ = 0;

    static uint16_t NextASID();

    //  Allocates and zeroes one 4KB table frame through the model's hook, records it in
    //      owned_, and returns it as a kernel VA.  Null on failure or when owned_ is full.

    uint64_t *AllocateTable(uint64_t &physical_out);

    //  Walks down to the L3 entry for user_va, creating L2/L3 tables when create is true.
    //      Returns nullptr if the walk cannot continue.

    uint64_t *L3EntryFor(uint64_t user_va, bool create);
    const uint64_t *L3EntryFor(uint64_t user_va) const;
};
