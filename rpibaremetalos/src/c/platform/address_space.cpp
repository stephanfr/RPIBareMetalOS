/**
 * Copyright 2026 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "platform/address_space.h"

#include <atomic>

#include "devices/log.h"

//  4KB granule, T0SZ = 25 (a 39-bit user VA):
//
//      L1 index = VA[38:30]   512 entries, 1GB each   -- only ONE is ever ours
//      L2 index = VA[29:21]   512 entries, 2MB each
//      L3 index = VA[20:12]   512 entries, 4KB each
//
//  MapPages() and Translate() touch only the L2/L3 chain hanging off
//      l1_[UserSpace().l1_index].  They never write another L1 slot, in any model --
//      under kernel_only_1_to_1 the other slots are aliases of the kernel's own tables
//      and writing through them would corrupt every task at once.

namespace
{
    constexpr uint32_t L1_SHIFT = 30;
    constexpr uint32_t L2_SHIFT = 21;
    constexpr uint32_t L3_SHIFT = 12;
    constexpr uint64_t TABLE_INDEX_MASK = 0x1FF;

    constexpr uint64_t L1IndexOf(uint64_t va) { return (va >> L1_SHIFT) & TABLE_INDEX_MASK; }
    constexpr uint64_t L2IndexOf(uint64_t va) { return (va >> L2_SHIFT) & TABLE_INDEX_MASK; }
    constexpr uint64_t L3IndexOf(uint64_t va) { return (va >> L3_SHIFT) & TABLE_INDEX_MASK; }

    //  A descriptor's next-level/output address field is bits[47:12] of a PHYSICAL
    //      address, i.e. physical >> 12.  The kernel's 2MB block code writes block << 9
    //      because (block * 2MB) >> 12 == block << 9; do not copy that shift here.

    constexpr uint64_t AddressField(uint64_t physical) { return physical >> 12; }

    //  Bits [47:12] only.  Shifting down and back up would keep the upper attribute bits
    //      (PXN at 53, UXN at 54, NSTable at 63) and hand back a nonsense address.

    constexpr uint64_t DESCRIPTOR_ADDRESS_MASK = 0x0000FFFFFFFFF000ULL;

    constexpr uint64_t DescriptorPhysical(uint64_t raw) { return raw & DESCRIPTOR_ADDRESS_MASK; }

    //  ASID allocator.
    //
    //  An ASID is a dense integer in [1, 65535], so a bitmap is the natural representation --
    //      and unlike a single-producer stack it is safe for concurrent allocate AND release
    //      from any core, which is what actually happens: any core can fork a user task, and
    //      BOTH the reaper (ReleaseResources) and the CLI (test addrspace) destroy address
    //      spaces.  There is no producer/consumer contract left to violate.
    //
    //  Claim is one fetch_or, release is one fetch_and -- no CAS loop, and no ABA, because
    //      the bit IS the ASID and there is no slot to recycle.  1024 words = 8KB.
    //
    //  Bit 0 is the kernel's TTBR0 ASID and is never handed out.

    constexpr uint32_t ASID_BITMAP_WORDS = 65536 / 64;

    minstd::atomic<uint64_t> asid_in_use[ASID_BITMAP_WORDS];

    //  Where the last claim landed, so a busy system does not rescan from zero.  A hint
    //      only -- correctness never depends on it.

    minstd::atomic<uint32_t> asid_hint(1);
}

uint16_t AddressSpace::NextASID()
{
    //  No wrap case and no vmalle1is: there is no monotonic counter to wrap, and every ASID
    //      is invalidated individually in ~AddressSpace() before it returns to the bitmap.

    const uint32_t start_word = (asid_hint.load(minstd::memory_order_relaxed) / 64) % ASID_BITMAP_WORDS;

    for (uint32_t scanned = 0; scanned < ASID_BITMAP_WORDS; scanned++)
    {
        const uint32_t word_index = (start_word + scanned) % ASID_BITMAP_WORDS;

        while (true)
        {
            const uint64_t word = asid_in_use[word_index].load(minstd::memory_order_relaxed);

            if (word == ~0ULL)
            {
                break;                                      //  full, move to the next word
            }

            const uint32_t bit = __builtin_ctzll(~word);    //  lowest clear bit
            const uint64_t mask = 1ULL << bit;
            const uint32_t candidate = (word_index * 64) + bit;

            if (candidate == KERNEL_ASID)
            {
                //  Claim bit 0 permanently so the scan stops finding it.  Self-healing on the
                //      first allocation -- no static initializer, so no .init_array ordering
                //      to reason about.

                asid_in_use[0].fetch_or(1ULL, minstd::memory_order_relaxed);
                continue;
            }

            if ((asid_in_use[word_index].fetch_or(mask, minstd::memory_order_acquire) & mask) == 0)
            {
                asid_hint.store(candidate, minstd::memory_order_relaxed);

                return (uint16_t)candidate;
            }

            //  Another core claimed that bit first -- retry this word.
        }
    }

    LogFatal("AddressSpace::NextASID - all 65535 ASIDs are in use\n");
    ParkCore();
}

uint64_t *AddressSpace::AllocateTable(uint64_t &physical_out)
{
    if (owned_count_ >= MAX_OWNED_BLOCKS)
    {
        return nullptr;
    }

    //  Through the model's hook, never GetMemoryManager() directly -- that indirection is
    //      what makes Phase 6 an implementation change with no call-site churn.

    MemoryPagePointer frame = MemoryModel::Instance().AllocateUserFrame(PAGE_SIZE);

    if (frame == 0)
    {
        return nullptr;
    }

    uint64_t *table = (uint64_t *)frame;            //  kernel VA: this is what we write through
    physical_out = frame.Physical();

    for (uint32_t i = 0; i < ENTRIES_PER_TABLE; i++)
    {
        table[i] = 0;
    }

    owned_[owned_count_++] = OwnedBlock{physical_out, PAGE_SIZE};

    return table;
}

bool AddressSpace::Initialize()
{
    const MemoryModel &model = MemoryModel::Instance();

    MemoryPagePointer l1_page = model.AllocateUserFrame(PAGE_SIZE);

    if (l1_page == 0)
    {
        return false;
    }

    l1_ = (uint64_t *)l1_page;                  //  kernel VA
    l1_physical_ = l1_page.Physical();
    owned_[owned_count_++] = OwnedBlock{l1_physical_, PAGE_SIZE};

    //  The model decides what a task's L1 starts as -- all zero under kernel_high_user_low,
    //      a verbatim copy of the kernel L1 under kernel_only_1_to_1.  A third model answers
    //      differently and nothing here changes.

    if (!model.SeedTaskL1(l1_))
    {
        return false;
    }

    //  The user window must land in a free L1 slot whatever the model just wrote.  Confirmed
    //      free on every board today (slots 0-3, plus RPi5's 64, 65 and 124-127, are the only
    //      ones ever written), but a future board or a future model could change that and the
    //      failure would be silent memory corruption.

    if (l1_[model.UserSpace().l1_index] != 0)
    {
        return false;
    }

    asid_ = NextASID();

    asm volatile("dsb ishst" ::: "memory");

    return true;
}

uint64_t *AddressSpace::L3EntryFor(uint64_t user_va, bool create)
{
    if (l1_ == nullptr)
    {
        return nullptr;
    }

    const UserSpaceLayout &layout = MemoryModel::Instance().UserSpace();

    //  Everything we own hangs off exactly one L1 slot; refuse anything else outright so a
    //      bad VA can never reach another slot's descriptors.

    if (L1IndexOf(user_va) != layout.l1_index)
    {
        return nullptr;
    }

    uint64_t *l1_entry = &l1_[layout.l1_index];
    uint64_t *l2 = nullptr;

    if (*l1_entry == 0)
    {
        if (!create)
        {
            return nullptr;
        }

        uint64_t l2_physical = 0;
        l2 = AllocateTable(l2_physical);

        if (l2 == nullptr)
        {
            return nullptr;
        }

        VMSAv8_64_DESCRIPTOR table_descriptor{};
        table_descriptor.Raw64 = 0;
        table_descriptor.EntryType = TableType::PAGE_TABLE;     //  bits[1:0] = 0b11
        table_descriptor.Address = AddressField(l2_physical);

        *l1_entry = table_descriptor.Raw64;
    }
    else
    {
        l2 = (uint64_t *)PhysicalToKernelVirtualAddress(DescriptorPhysical(*l1_entry));
    }

    uint64_t *l2_entry = &l2[L2IndexOf(user_va)];
    uint64_t *l3 = nullptr;

    if (*l2_entry == 0)
    {
        if (!create)
        {
            return nullptr;
        }

        uint64_t l3_physical = 0;
        l3 = AllocateTable(l3_physical);

        if (l3 == nullptr)
        {
            return nullptr;
        }

        VMSAv8_64_DESCRIPTOR table_descriptor{};
        table_descriptor.Raw64 = 0;
        table_descriptor.EntryType = TableType::PAGE_TABLE;
        table_descriptor.Address = AddressField(l3_physical);

        *l2_entry = table_descriptor.Raw64;
    }
    else
    {
        l3 = (uint64_t *)PhysicalToKernelVirtualAddress(DescriptorPhysical(*l2_entry));
    }

    return &l3[L3IndexOf(user_va)];
}

const uint64_t *AddressSpace::L3EntryFor(uint64_t user_va) const
{
    return const_cast<AddressSpace *>(this)->L3EntryFor(user_va, false);
}

bool AddressSpace::MapPages(uint64_t user_va, uint64_t physical_base, uint64_t size,
                            Stage2AccessPermission ap, bool executable)
{
    const UserSpaceLayout &layout = MemoryModel::Instance().UserSpace();

    if ((size == 0) ||
        ((user_va & (PAGE_SIZE - 1)) != 0) ||
        ((physical_base & (PAGE_SIZE - 1)) != 0) ||
        ((size & (PAGE_SIZE - 1)) != 0))
    {
        return false;
    }

    //  Overflow-safe range check against the user window.  Nothing below USER_IMAGE_BASE is
    //      mappable: the gap under it is the guard that makes a small-offset null pointer
    //      fault in both models.

    if ((user_va < layout.image_base) ||
        (user_va > layout.space_top) ||
        (size > layout.space_top - user_va))
    {
        return false;
    }

    //  Refuse BEFORE mapping anything: on a full owned_ the block could not be recorded,
    //      and an unrecorded block is exactly the leak this check exists to prevent.

    if (owned_count_ >= MAX_OWNED_BLOCKS)
    {
        return false;
    }

    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE)
    {
        uint64_t *entry = L3EntryFor(user_va + offset, true);

        if (entry == nullptr)
        {
            return false;
        }

        VMSAv8_64_DESCRIPTOR page{};
        page.Raw64 = 0;

        //  A page descriptor at level 3 shares the table descriptor's bits[1:0] = 0b11
        //      encoding; PAGE_TABLE is the right enumerator here despite the name.

        page.EntryType = TableType::PAGE_TABLE;
        page.MemAttr = MemoryAttribute::NORMAL;
        page.S2AP = ap;
        page.SH = Stage2Sharability::INNER_SHAREABLE;
        page.AF = AccessFlag::ACCESSED;
        page.NonGlobalFlag = 1;                             //  per-ASID, not global
        page.Address = AddressField(physical_base + offset);
        page.PXN = 1;                                       //  EL1 never executes user pages
        page.UXN = executable ? 0 : 1;

        *entry = page.Raw64;
    }

    //  Make every descriptor written above visible to the table walker before this space
    //      can be installed.

    asm volatile("dsb ishst" ::: "memory");

    owned_[owned_count_++] = OwnedBlock{physical_base, size};

    return true;
}

bool AddressSpace::Translate(uint64_t user_va, uint64_t &physical_out) const
{
    const UserSpaceLayout &layout = MemoryModel::Instance().UserSpace();

    //  Range check FIRST, walk second, always -- see the header.  Under
    //      kernel_only_1_to_1 this table really does map the kernel identity range, and
    //      only this check keeps a user pointer of 0x80000 out.

    if ((user_va < layout.space_base) || (user_va >= layout.space_top))
    {
        return false;
    }

    const uint64_t *entry = L3EntryFor(user_va);

    if ((entry == nullptr) || (*entry == 0))
    {
        return false;
    }

    physical_out = DescriptorPhysical(*entry) | (user_va & (PAGE_SIZE - 1));

    return true;
}

bool AddressSpace::TranslateForWrite(uint64_t user_va, uint64_t &physical_out) const
{
    const UserSpaceLayout &layout = MemoryModel::Instance().UserSpace();

    if ((user_va < layout.space_base) || (user_va >= layout.space_top))
    {
        return false;
    }

    const uint64_t *entry = L3EntryFor(user_va);

    if ((entry == nullptr) || (*entry == 0))
    {
        return false;
    }

    VMSAv8_64_DESCRIPTOR desc{};
    desc.Raw64 = *entry;

    //  ALLOWLIST, not a blocklist: EL1_READ_WRITE_EL0_READ_WRITE is the only encoding EL0 may
    //      write through.  EL1_READ_WRITE (S2AP 0) is EL1-only with NO EL0 access and must not
    //      become a valid destination merely by not being read-only.  UXN == 0 additionally
    //      refuses executable pages, so no I-cache maintenance is ever needed after a write.

    if ((desc.S2AP != EL1_READ_WRITE_EL0_READ_WRITE) || (desc.UXN == 0))
    {
        return false;
    }

    physical_out = DescriptorPhysical(*entry) | (user_va & (PAGE_SIZE - 1));

    return true;
}

AddressSpace::~AddressSpace()
{
    //  Return ASID before releasing pages so it can be reused immediately.

    if (asid_ != KERNEL_ASID)
    {
        //  Invalidate every TLB entry tagged with this ASID BEFORE it becomes reusable.
        //      The frames released below are about to be handed to another space, and a
        //      stale entry would let the next holder of this ASID read them.
        //
        //  This is the ONLY place the invalidation can happen -- NextASID() has no flush of
        //      its own, because per-ASID invalidation here is strictly cheaper than the
        //      whole-TLB vmalle1is the old monotonic-counter wrap needed.
        //
        //  ASID goes in bits [63:48] of the operand -- the same layout TTBR0Value() uses.

        asm volatile("dsb ishst" ::: "memory");
        asm volatile("tlbi aside1is, %0" ::"r"((uint64_t)asid_ << 48) : "memory");
        asm volatile("dsb ish" ::: "memory");
        asm volatile("isb" ::: "memory");

        //  Release ONLY after the invalidation above has completed on every core -- this is
        //      what makes the ASID visible to NextASID() again.  No lock: fetch_and is safe
        //      from any core against any other core's fetch_or.

        asid_in_use[asid_ / 64].fetch_and(~(1ULL << (asid_ % 64)), minstd::memory_order_release);

        asid_ = KERNEL_ASID;    //  prevent double-claim if destructor is re-entered
    }
}
