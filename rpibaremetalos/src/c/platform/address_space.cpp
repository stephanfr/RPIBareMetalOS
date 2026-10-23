/**
 * Copyright 2026 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "platform/address_space.h"

#include <atomic>
#include <string.h>

#include "devices/log.h"

//  4KB granule, T0SZ = 25 (a 39-bit user VA):
//
//      L1 index = VA[38:30]   512 entries, 1GB each   -- only [l1_index_start, l1_index_end] are ours
//      L2 index = VA[29:21]   512 entries, 2MB each
//      L3 index = VA[20:12]   512 entries, 4KB each
//
//  MapPages(), FaultInUserPage(), Translate() and the ~AddressSpace walk touch only the
//      L2/L3 chains hanging off the window's L1 slots.  They never read or write another L1 slot.

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

uint32_t AddressSpace::AllocatedASIDCount()
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < ASID_BITMAP_WORDS; i++)
    {
        count += (uint32_t)__builtin_popcountll(asid_in_use[i].load(minstd::memory_order_relaxed));
    }

    //  Bit 0 is the kernel's, claimed permanently on the first allocation.

    if ((asid_in_use[0].load(minstd::memory_order_relaxed) & 1ULL) != 0)
    {
        count--;
    }

    return count;
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
    //  Through MemoryModel::AllocateUserFrame, never GetMemoryManager() directly, so a change
    //      to how user frames are sourced (a dedicated pool, accounting) touches no call site.

    MemoryPagePointer frame = MemoryModel::AllocateUserFrame(PAGE_SIZE);

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

    return table;
}

bool AddressSpace::Initialize()
{
    MemoryPagePointer l1_page = MemoryModel::AllocateUserFrame(PAGE_SIZE);

    if (l1_page == 0)
    {
        return false;
    }

    l1_ = (uint64_t *)l1_page;                  //  kernel VA
    l1_physical_ = l1_page.Physical();
    owned_[owned_count_++] = OwnedBlock{l1_physical_, PAGE_SIZE};

    //  A task's L1 starts ALL ZERO: the kernel lives entirely in TTBR1, so a task sees its own
    //      window and nothing else.  From here on, every non-zero slot was written by this
    //      space.

    for (uint32_t i = 0; i < ENTRIES_PER_TABLE; i++)
    {
        l1_[i] = 0;
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

    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    //  Everything we own hangs off the window's L1 slots; refuse anything else outright so a
    //      bad VA can never reach another slot's descriptors.

    const uint64_t l1_index = L1IndexOf(user_va);

    if ((l1_index < layout.l1_index_start) || (l1_index > layout.l1_index_end))
    {
        return nullptr;
    }

    uint64_t *l1_entry = &l1_[l1_index];
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
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    if ((size == 0) ||
        ((user_va & (PAGE_SIZE - 1)) != 0) ||
        ((physical_base & (PAGE_SIZE - 1)) != 0) ||
        ((size & (PAGE_SIZE - 1)) != 0))
    {
        return false;
    }

    //  Overflow-safe range check against the user window.  Nothing below USER_IMAGE_BASE is
    //      mappable: the gap under it is the guard that makes a small-offset null pointer
    //      fault.

    if ((user_va < layout.image_base) ||
        (user_va > layout.space_top) ||
        (size > layout.space_top - user_va))
    {
        return false;
    }

    //  The guard gap is never mapped: refuse any mapping that would overlap it, so a bug
    //      cannot silently turn the safety gap into live memory.  (user_va + size cannot
    //      overflow -- the check above bounds it by space_top.)

    if ((user_va < layout.stack_bottom) && ((user_va + size) > layout.guard_bottom))
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
            //  The caller releases the block on false, so the descriptors already written
            //      must not keep pointing at it.  No TLBI: MapPages only runs on a space that
            //      is not yet installed (loader, test addrspace), so nothing is cached.

            for (uint64_t undo = 0; undo < offset; undo += PAGE_SIZE)
            {
                *L3EntryFor(user_va + undo, false) = 0;
            }

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

bool AddressSpace::FaultInUserPage(uint64_t user_va, uint64_t heap_break)
{
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    const uint64_t page_va = user_va & ~(PAGE_SIZE - 1);

    //  Only the LIVE heap and the stack are demand-paged.  The image is mapped eagerly at
    //      load; heap above the break, the guard gap and everything else must keep faulting.

    const bool in_heap = (page_va >= layout.heap_base) && (page_va < heap_break) && (heap_break <= layout.heap_top);
    const bool in_stack = (page_va >= layout.stack_bottom) && (page_va < layout.stack_top);

    if (!in_heap && !in_stack)
    {
        return false;
    }

    uint64_t *entry = L3EntryFor(page_va, true);

    if ((entry == nullptr) || (*entry != 0))                                    //  null: no table; nonzero: already mapped
    {
        return false;
    }

    MemoryPagePointer frame = MemoryModel::AllocateUserFrame(PAGE_SIZE);

    if (frame == 0)
    {
        return false;                                                           //  OOM: caller kills the task
    }

    //  GetFreeBlock does not clear memory.  Without this the previous owner's stack or heap
    //      contents would be readable by this task.

    memset((uint8_t *)frame, 0, PAGE_SIZE);

    VMSAv8_64_DESCRIPTOR page{};
    page.Raw64 = 0;
    page.EntryType = TableType::PAGE_TABLE;                                     //  level-3 page descriptor (bits[1:0] = 0b11)
    page.MemAttr = MemoryAttribute::NORMAL;
    page.S2AP = Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE;
    page.SH = Stage2Sharability::INNER_SHAREABLE;
    page.AF = AccessFlag::ACCESSED;                                             //  set now, or the retry takes an access-flag fault
    page.NonGlobalFlag = 1;                                                     //  per-ASID, not global
    page.Address = AddressField(frame.Physical());
    page.PXN = 1;                                                               //  EL1 never executes user pages
    page.UXN = 1;                                                               //  heap/stack are never executable
    page.SoftwareDemandPaged = 1;                                               //  ~AddressSpace frees this leaf on the walk

    *entry = page.Raw64;

    //  No TLBI: the entry was invalid, and invalid entries are never cached.  The dsb orders
    //      the zeroing and the descriptor before the retry; the eret back to EL0 is context
    //      synchronizing.

    asm volatile("dsb ish" ::: "memory");

    return true;
}

bool AddressSpace::Translate(uint64_t user_va, uint64_t &physical_out) const
{
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    //  Range check FIRST, walk second, always -- see the header.

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
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

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

    //  Walk ONLY the window's L1 slots.  Initialize() zeroed the whole L1 before anything was
    //      mapped, so every table and demand-paged leaf under them is ours; l1_ is non-null
    //      exactly when Initialize() got that far.  Leaves without SoftwareDemandPaged belong
    //      to an owned_ block (image) and are freed whole below, not page by page.

    if (l1_ != nullptr)
    {
        const UserSpaceLayout &layout = MemoryModel::UserSpace();

        for (uint64_t slot = layout.l1_index_start; slot <= layout.l1_index_end; slot++)
        {
            const uint64_t l1v = l1_[slot];

            if ((l1v & 0x3) != (uint64_t)TableType::PAGE_TABLE)       //  empty (we never write blocks here)
            {
                continue;
            }

            uint64_t *l2 = (uint64_t *)PhysicalToKernelVirtualAddress(DescriptorPhysical(l1v));

            for (uint32_t i = 0; i < ENTRIES_PER_TABLE; i++)
            {
                const uint64_t l2v = l2[i];

                if ((l2v & 0x3) != (uint64_t)TableType::PAGE_TABLE)
                {
                    continue;
                }

                const uint64_t *l3 = (const uint64_t *)PhysicalToKernelVirtualAddress(DescriptorPhysical(l2v));

                for (uint32_t j = 0; j < ENTRIES_PER_TABLE; j++)
                {
                    VMSAv8_64_DESCRIPTOR leaf{};
                    leaf.Raw64 = l3[j];

                    if (((l3[j] & 0x3) == (uint64_t)TableType::PAGE_TABLE) && leaf.SoftwareDemandPaged)
                    {
                        MemoryModel::ReleaseUserFrame(MemoryPagePointer{DescriptorPhysical(l3[j])}, PAGE_SIZE);
                    }
                }

                MemoryModel::ReleaseUserFrame(MemoryPagePointer{DescriptorPhysical(l2v)}, PAGE_SIZE);    //  the L3 table
            }

            MemoryModel::ReleaseUserFrame(MemoryPagePointer{DescriptorPhysical(l1v)}, PAGE_SIZE);        //  the L2 table
        }
    }

    //  Now owned_: the L1 (read by the walk above, so freed only now) and every MapPages block.

    for (uint32_t i = 0; i < owned_count_; i++)
    {
        MemoryModel::ReleaseUserFrame(MemoryPagePointer{owned_[i].physical}, owned_[i].size);
    }

    owned_count_ = 0;
    l1_ = nullptr;
    l1_physical_ = 0;
}
