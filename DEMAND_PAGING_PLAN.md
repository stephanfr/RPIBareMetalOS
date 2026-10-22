# One Memory Model, then Demand Paging + Multi-Slot User Layout — Implementation Plan

> **Status:** implementation guide. Every code block below is **before → after** against the
> current tree. Make the edits yourself, top to bottom. Nothing here has been applied yet.
>
> **Scope:**
> 1. **Phase 0** retires the `kernel_only_1_to_1` memory model and removes the machinery for
>    choosing between models. The isolated model (`kernel_high_user_low`) has been verified on
>    real Pi 3/4/5 hardware and is already the kernel's default. After Phase 0 it is the only
>    model, and `MemoryModel` is a plain class of static functions rather than an interface.
> 2. **Phases A–F** add demand paging for the **user heap and user stack** and widen the user
>    window to 64 GB. **Kernel memory is never demand-paged** (see §2.1).
>
> **Delete this file before committing** (or add it to `.gitignore`). See `AGENTS.md`,
> Pre-Commit Cleanup.

### How to use this guide

- **Phase 0 (§2A) comes first, and is its own commit.** Every **Before** block in Phases A–F
  assumes Phase 0 has been applied. Finish, verify and commit Phase 0 before starting Phase A.
- Apply the phases **in order** (0 → A → F). Each **Before** block is a verbatim excerpt of the
  tree you can search for; replace it with the **After** block. Tabs vs. spaces in `.S` files
  and in the descriptor bit-field comments may not survive copy/paste exactly, so match on the
  text, not the whitespace.
- "Insert" steps say exactly where the new code goes, relative to an existing anchor.
- The **Build checkpoints** say when the tree is expected to compile. Phase 0 must build and pass
  regression on its own. Phases A–D deliberately leave the tree broken; it must build after
  Phase E.
- §9 lists the tests and §9.1 how to prove they actually detect the bugs they target.

---

## 1. Goals

1. **One memory model, no model abstraction.** Remove `kernel_only_1_to_1`. It was a bring-up
   safety net that let raw physical-address dereferences silently succeed. It doubled the
   regression matrix, and it forced the user window away from low addresses. Its debugging
   value is replaced by a fault hint (§2A, step 0.13). With no second model in prospect, the
   enum, the `memory_model=` setting and the virtual `MemoryModel` hierarchy go too.
2. A single, identical virtual-memory layout for every user task: program at the bottom,
   heap growing up from just after it, a **guard gap**, then the stack growing down from the
   top. Each task gets the *same* VAs mapped to *different* physical frames (already true —
   each task owns its own `AddressSpace` + ASID).
3. **Demand paging:** physical frames are allocated **on first touch** for both the heap and
   the stack. The first touch can come from EL0 (a translation fault) or from the kernel
   copying through a syscall pointer (a `Translate` miss in `user_access.cpp`). Untouched
   allocations cost nothing.
4. Expand the user window so it **spans multiple L1 slots** (it currently must fit one).

## 2. Design decisions (fixed for this plan)

| # | Decision | Value | Where it lives |
|---|----------|-------|----------------|
| 1 | Window size | **64 GB** (spans L1 index 256 **through** 319, 64 slots) | `TOTAL_USER_SPACE_IN_MB` |
| 2 | Image offset | **4 MB** (unchanged — `user.ld` stays valid) | `USER_IMAGE_OFFSET_MB` |
| 3 | Heap base | **256 MB** (unchanged) | `USER_HEAP_OFFSET_MB` |
| 4 | Guard gap | **1 GB**, never mapped | `USER_GUARD_SIZE_MB` |
| 5 | Max stack (per-task ceiling) | **1 GB**, demand-paged | `USER_MAX_STACK_SIZE_MB` |
| 6 | Memory models | **one** — `kernel_high_user_low`, with no selection machinery. `MemoryModel` is a non-instantiable class of static functions (`BootTTBR0Physical`, `UserSpace`, `AllocateUserFrame`, `ReleaseUserFrame`). `memory_model=` on the command line is ignored, with a boot notice | `memory_model.h`, `main.cpp` |
| 7 | Heap fault-in range | `[heap_base, heap_break)`. A page **above the break** is a wild pointer and kills the task | `FaultInUserPage(va, heap_break)` |
| 8 | OOM at fault time | **kill the task** (log + `Exit()`), same as any other fault | `FaultInUserPage` returns false → asm falls through |
| 9 | Fresh frames | **zeroed** before mapping (no cross-task data leak) | `FaultInUserPage` |
| 10 | Ownership | `owned_` = the L1 + `MapPages` blocks (image text/data) only. L2/L3 tables and demand-paged leaves are freed by a **walk of the window's L1 slots** | `~AddressSpace` |
| 11 | Demand-leaf marker | software descriptor bit 55 (`SoftwareDemandPaged`); the hardware ignores bits [58:55] | `VMSAv8_64_DESCRIPTOR` |
| 12 | Window base | **stays at 256 GB** in this plan. It existed to clear the identity map; moving it lower is a separate, optional follow-up (it changes `user.ld`, `hello.c` and the tests) | `USER_SPACE_BASE` |

All magnitudes in decisions 1–5 are plain `#define`s in `os_memory_config.h`, so you can
retune them freely. The `static_assert`s in A2 guard the ordering and the T0SZ range, and the
code doesn't depend on the window size: every slot loop runs over
`[l1_index_start, l1_index_end]`.

**The window is a virtual-address budget, not a memory reservation.** It bounds everything a
task can address (image + heap + stack, and any future mmap), but it costs nothing until it's
touched. An L2 table is allocated per touched 1 GB, an L3 table per touched 2 MB, and a frame
per touched 4 KB, so a 64 GB window costs exactly what the old 512 MB one did for the same
working set. Physical RAM (1–16 GB depending on the board) stays the real limit, as on Linux.
The limits on resizing:
- **Up to 256 GB** (L1 slots 256–511): change only the `#define`s here and the hardcoded
  addresses in `hello.c`.
- **Beyond 256 GB:** with the identity model gone, a task's L1 has nothing outside the window,
  so the window could start near 0 and use all 512 GB (decision 12). Beyond 512 GB means 48-bit
  user VAs (`T0SZ = 16`, which adds a fourth table level). Both are out of scope.

### 2.1 Why kernel memory is NOT demand-paged

Kernel heap, kernel stacks, page tables and the linear map stay wired:

- **Faults would come from contexts that cannot service them.** These include IRQ handlers,
  code holding a spinlock or running with IRQs masked, the memory manager itself, and the fault
  handler's own stack. Each would need a "can I fault here?" rule, and getting one wrong is a
  core-killing EL1 fault, not a killed task.
- **Nothing to gain.** Kernel memory is small, allocated deliberately, and always hot, so lazy
  allocation saves no real RAM. Linux and similar kernels keep kernel memory resident for the
  same reasons.
- **Better follow-up:** an unmapped **guard page under each kernel stack**. It turns a kernel
  stack overflow into an immediate, diagnosable EL1 fault instead of silent corruption, at the
  cost of one page of VA per task. It is out of scope here.

### 2.2 Why the teardown walk is safe

`AddressSpace::Initialize()` zeroes the task's L1 itself (Phase 0) before anything can be
mapped. So everything hanging off `[l1_index_start, l1_index_end]` was allocated by this space,
and the destructor may walk and free it. The walk runs only when `l1_` is non-null, which is
exactly when `Initialize()` got as far as zeroing it. It never reads a slot outside the window:
those are the only slots this code ever writes, and it keeps teardown proportional to the
window rather than the whole L1.

### Why the numbers work

```
base = 0x40_0000_0000  (256 GB, L1 index 256)

  +0      ┌──────────────────────────────┐  USER_SPACE_BASE    (256GB)  L1 slot 256 begins
  +4MB    │  image (header+text RO+X,     │  USER_IMAGE_BASE
          │          data+bss RW+XN)      │
  +256MB  ├──────────────────────────────┤  USER_HEAP_BASE               heap grows UP
          │                              │
          │      heap (up to ~61.75GB)   │
  +62GB   ├──────────────────────────────┤  USER_HEAP_TOP == USER_GUARD_BOTTOM   (318GB, slot 318)
          │       guard gap (1GB)        │  never mapped -> wild pointer FAULTS
  +63GB   ├──────────────────────────────┤  USER_STACK_BOTTOM                    (319GB, slot 319)
          │        stack (1GB)           │
  +64GB   └──────────────────────────────┘  USER_STACK_TOP == USER_SPACE_TOP     (320GB) slot 319 ends
```

| Region | Range (offset from base) | Size | L1 slots |
|---|---|---|---|
| unmapped (NULL guard) | `[0, 4MB)` | 4 MB | 256 |
| image | `[4MB, 256MB)` | ≤ 252 MB | 256 |
| heap | `[256MB, 62GB)` | 61.75 GB | 256–317 |
| guard gap | `[62GB, 63GB)` | 1 GB | 318 |
| stack | `[63GB, 64GB)` | 1 GB | 319 |

Window = 64 GB → L1 slots 256–319, i.e. VA `0x40_0000_0000`–`0x50_0000_0000`. That is well
inside the T0SZ=25 (512 GB, slots 0–511) TTBR0 range. A task's L1 starts all zero, and
`Initialize()` checks all 64 window slots at runtime.

The guard gap and the stack each occupy exactly one whole L1 slot. So a stray pointer into the
guard never even allocates an L2 table, because `FaultInUserPage` refuses it before
`L3EntryFor(create)` runs. And the stack's tables never share an L2 with the heap's.

---

## 2A. Phase 0 — One memory model, no model abstraction

**Why.** The isolated model (`kernel_high_user_low`) runs on real Pi 3/4/5 hardware and is
already the kernel's default. The identity model only differs in `BootTTBR0Physical` and
`SeedTaskL1`. Everything else is just a name that gets printed. What remains of its value (a
raw physical-address dereference silently succeeds) hides bugs rather than finding them. It
costs two of the four regression passes per board, a "same outcome under both models"
constraint on every test, and the 256 GB window placement.

With one model left, the selection machinery has nothing to select. There's no credible second
model for these cores: KPTI isn't needed (none of A53/A72/A76 has Meltdown), and PAN is a CPU
feature toggle, not an address-space policy. So the machinery goes too: the `MemoryModelTypes`
enum, its strings and `ToString`, `MMUManager::MemoryModel()`, the `memory_model=` setting, the
virtual `MemoryModel` hierarchy, and its placement-new singleton. If a second model ever turns
up, the seam is in git history (any commit before Phase 0).

**Rules for this phase**
- **`MemoryModel` stays as a name, not an interface.** It becomes a non-instantiable class of
  static functions: `BootTTBR0Physical()`, `UserSpace()`, `AllocateUserFrame()`,
  `ReleaseUserFrame()`. That keeps the policy written down in one header, with no virtual
  dispatch and nothing to construct at boot.
- **Keep `AllocateUserFrame` / `ReleaseUserFrame` as the single choke point for user frames.**
  Every task table, image block, heap page and stack page goes through them. Phases B and E
  depend on that.
- **`SeedTaskL1` is gone.** `AddressSpace::Initialize()` zeroes the L1 itself. Because the L1
  always starts all zero, the "is the window slot still free?" check after seeding can't fail,
  and it goes too.
- **`memory_model=` on the kernel command line is ignored.** An old `cmdline.txt` boots whatever
  value it holds, and `main.cpp` prints one notice that the setting is obsolete. This is a
  behaviour change: an unknown value used to park the core.
- **Keep the `__boot_ttbr0_base` published value.** `start.S` and
  `AddressSpace::KernelTTBR0Value()` read it. Only the code that computes it changes.
- **Keep the boot-time identity map.** `start.S` / `mmu.S` build their own early tables for the
  MMU-enable transition. That isn't the memory model and stays. Also leave `Stage2map1to1_`
  (board memory managers) and the `map1to1` parameter of `EnableMMUTables` alone: they name
  the kernel's identity *table*, which also serves as the TTBR1 linear map.
- **Every remaining mention of the old name says "retired".** The Phase 0 check (0.17) relies
  on that.
- The user window **stays at 256 GB** (decision 12).

Apply the steps in order: 0.7(a) rewrites `AddressSpace::Initialize()` before 0.7(b)'s
mechanical rename runs over the same file.

### 0.1 `rpibaremetalos/include/platform/mmu_manager.h`

**(a)** Enum and strings. **Before**
```cpp
    typedef enum class MemoryModelTypes : uint32_t
    {
        UNKNOWN = 0,
        KERNEL_ONLY_1_TO_1,
        KERNEL_HIGH_USER_LOW
    } MemoryModelTypes;

    static constexpr uint32_t MAX_RESERVED_MEMORY_REGIONS = 4;
    static constexpr const char* UNKNOWN_STRING = "unknown";
    static constexpr const char* KERNEL_ONLY_1_TO_1_STRING = "kernel_only_1_to_1";
    static constexpr const char* KERNEL_HIGH_USER_LOW_STRING = "kernel_high_user_low";
```
**After**
```cpp
    static constexpr uint32_t MAX_RESERVED_MEMORY_REGIONS = 4;
```

**(b)** The accessor. **Before**
```cpp
    virtual ReservedMemoryRegion ReservedMemoryRegionAt(uint32_t index) const = 0;
    virtual MemoryModelTypes MemoryModel() const = 0;
```
**After**
```cpp
    virtual ReservedMemoryRegion ReservedMemoryRegionAt(uint32_t index) const = 0;
```

**(c)** Delete `ToString` (everything after the closing `};` of the class). **Before**
```cpp
inline const char *ToString(MMUManager::MemoryModelTypes model)
{
    switch(  model )
    {
        case MMUManager::MemoryModelTypes::UNKNOWN:
            return MMUManager::UNKNOWN_STRING;
            
        case MMUManager::MemoryModelTypes::KERNEL_ONLY_1_TO_1:
            return MMUManager::KERNEL_ONLY_1_TO_1_STRING;

        case MMUManager::MemoryModelTypes::KERNEL_HIGH_USER_LOW:
            return MMUManager::KERNEL_HIGH_USER_LOW_STRING;
    }

    return MMUManager::UNKNOWN_STRING;
}
```
**After:** nothing. The file ends at the class's closing `};`.

> `MMUManager::KernelPageTableL1()` loses its only caller (the deleted identity model's
> `SeedTaskL1`). Leave it: it is part of the `MMUManager` interface and is cheap to keep. Remove
> it in a later cleanup if nothing else picks it up.

### 0.2 `rpibaremetalos/src/c/platform/mmu_manager.cpp`

**(a)** Drop `#include "platform/memory_model.h"` (the board headers still reach it through
`platform_mmu.h`). **Before**
```cpp
#include "platform/kernel_command_line.h"
#include "platform/memory_model.h"
```
**After**
```cpp
#include "platform/kernel_command_line.h"
```

**(b)** Stop parsing `memory_model=`. **Before**
```cpp
    //  Get the memory model from the kernel command line

    MemoryModelTypes memory_model = MemoryModelTypes::KERNEL_HIGH_USER_LOW;

    minstd::fixed_string<64> memory_model_string;

    if( KernelCommandLine::FindSetting("memory_model", memory_model_string))
    {
        if( memory_model_string == KERNEL_ONLY_1_TO_1_STRING)
        {
            memory_model = MemoryModelTypes::KERNEL_ONLY_1_TO_1;
        }
        else if( memory_model_string == KERNEL_HIGH_USER_LOW_STRING)
        {
            memory_model = MemoryModelTypes::KERNEL_HIGH_USER_LOW;
        }
        else
        {
            ParkCore();
        }
    }

    //  Check for a "strict_align=1" kernel command-line opt-in to SCTLR_EL1.A
```
**After**
```cpp
    //  Check for a "strict_align=1" kernel command-line opt-in to SCTLR_EL1.A
```

**(c)** No model to construct, and the board constructors take no argument. **Before**
```cpp
    //  Construct the memory model BEFORE the board memory manager, EnableMMU() requires it exist.

    MemoryModel::Initialize(memory_model);

    //  Create the platform specific memory manager
    //      Use placement new to create the object in the storage buffer allocaated above.

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI3BPlusMemoryManager(memory_model);
            break;

        case RPI_BOARD_ENUM_RPI4 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI4BMemoryManager(memory_model);
            break;

        case RPI_BOARD_ENUM_RPI5 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI5MemoryManager(memory_model);
            break;
```
**After**
```cpp
    //  Create the platform specific memory manager
    //      Use placement new to create the object in the storage buffer allocaated above.

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI3BPlusMemoryManager();
            break;

        case RPI_BOARD_ENUM_RPI4 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI4BMemoryManager();
            break;

        case RPI_BOARD_ENUM_RPI5 :
            platform_memory_manager_ = new( (void*)__mmu_manager_storage ) RPI5MemoryManager();
            break;
```

### 0.3 `rpibaremetalos/include/platform/memory_model.h`

Replace everything from the first comment after the `#include`s to the end of the file. The
`#include "platform/mmu_manager.h"` line goes too, because nothing here needs `MMUManager` any
more. (Every file that includes `memory_model.h` and uses `MMUManager` includes
`mmu_manager.h` or `platform_mmu.h` itself.)

**After** (the whole file)
```cpp
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
    uint64_t l1_index;          //  space_base >> 30
    uint64_t image_base;        //  header + text
    uint64_t heap_base;         //  sc_Malloc grows up from here
    uint64_t stack_top;         //  user stack grows down from here
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
        static const UserSpaceLayout layout{
            USER_SPACE_BASE,
            USER_SPACE_TOP,
            USER_SPACE_L1_INDEX,
            USER_IMAGE_BASE,
            USER_HEAP_BASE,
            USER_STACK_TOP};

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
```
> Gone: `SharedUserSpaceLayout()` (folded into `UserSpace()`), `TASK_L1_ENTRY_COUNT` (only
> `SeedTaskL1` used it), `Type()`, `Name()`, `SeedTaskL1()`, `Initialize()`, `Instance()`,
> `IsInitialized()`, and both subclasses.

### 0.4 `rpibaremetalos/src/c/platform/memory_model.cpp`

**Delete the file.** It held only the placement-new storage and `MemoryModel::Initialize()`.
The aarch64 build picks up sources by wildcard, and the host test build (`Makefile.test.mk`)
never compiled it, so no makefile changes.

### 0.5 `rpibaremetalos/include/platform/platform_mmu.h`

**(a)** **Before**
```cpp
class AARCH64PlatformMemoryManager : public MMUManager
{
    using MemoryModelTypes = MMUManager::MemoryModelTypes;

    protected:
```
**After**
```cpp
class AARCH64PlatformMemoryManager : public MMUManager
{
    protected:
```

**(b)** Constructor. **Before**
```cpp
    AARCH64PlatformMemoryManager(MemoryModelTypes memory_model)
        : memory_model_(memory_model)
    {
```
**After**
```cpp
    AARCH64PlatformMemoryManager()
    {
```

**(c)** **Before**
```cpp
    AARCH64PlatformMemoryManager() = delete;

    MemoryModelTypes MemoryModel() const override
    {
        return memory_model_;
    }

    uint64_t AllocatableMemoryTop() const override
```
**After**
```cpp
    uint64_t AllocatableMemoryTop() const override
```

**(d)** **Before**
```cpp
protected:
    MemoryModelTypes memory_model_;

    uint32_t board_revision_;
```
**After**
```cpp
protected:
    uint32_t board_revision_;
```

**(e)** `PublishBootPageTableBases`. **Before**
```cpp
    //  TTBR0 is the ONLY thing a memory model changes at boot.  The model answers; this
    //      code does not branch, and start.S installs a published value and never learns
    //      that models exist.  A third model changes nothing below this line.

    void PublishBootPageTableBases(uint64_t kernel_page_table_va)
    {
        __kernel_page_table_base = KernelVirtualAddressToPhysical(kernel_page_table_va);

        __boot_ttbr0_base = MemoryModel::Instance().BootTTBR0Physical((const uint64_t *)kernel_page_table_va);
```
**After**
```cpp
    //  TTBR0 at boot is MemoryModel's empty user table; start.S installs the published value.

    void PublishBootPageTableBases(uint64_t kernel_page_table_va)
    {
        __kernel_page_table_base = KernelVirtualAddressToPhysical(kernel_page_table_va);

        __boot_ttbr0_base = MemoryModel::BootTTBR0Physical();
```

### 0.6 Board memory managers (RPi3, RPi4, RPi5)

**(a)** `include/platform/rpi3/rpi3_memory_manager.h`. **Before**
```cpp
    using VMSAv8_64_DESCRIPTOR = AARCH64PlatformMemoryManager::VMSAv8_64_DESCRIPTOR;
    using MemoryModelTypes = MMUManager::MemoryModelTypes;

public:
    RPI3BPlusMemoryManager(MemoryModelTypes memory_model);

    void EnableMMU() override
    {
        //  Decides TTBR0 from the memory model and publishes both bases for the
        //      secondary cores.  Both arguments below are PHYSICAL (R1).
```
**After**
```cpp
    using VMSAv8_64_DESCRIPTOR = AARCH64PlatformMemoryManager::VMSAv8_64_DESCRIPTOR;

public:
    RPI3BPlusMemoryManager();

    void EnableMMU() override
    {
        //  Publishes the kernel and boot-TTBR0 table bases for the secondary cores.  Both
        //      arguments below are PHYSICAL (R1).
```
Make the same edit in `include/platform/rpi4/rpi4_memory_manager.h` (`RPI4BMemoryManager`)
and `include/platform/rpi5/rpi5_memory_manager.h` (`RPI5MemoryManager`). The blocks are
identical apart from the class name.

**(b)** `src/c/platform/rpi3/rpi3_memory_manager.cpp`. **Before**
```cpp
RPI3BPlusMemoryManager::RPI3BPlusMemoryManager(MemoryModelTypes memory_model)
    : AARCH64PlatformMemoryManager(memory_model)
{
```
**After**
```cpp
RPI3BPlusMemoryManager::RPI3BPlusMemoryManager()
{
```
Same for `src/c/platform/rpi4/rpi4_memory_manager.cpp` (`RPI4BMemoryManager::RPI4BMemoryManager`)
and `src/c/platform/rpi5/rpi5_memory_manager.cpp` (`RPI5MemoryManager::RPI5MemoryManager`).

### 0.7 Call sites

**(a)** `src/c/platform/address_space.cpp`: `Initialize()` zeroes its own L1. Do this **before**
(b). **Before**
```cpp
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
```
**After**
```cpp
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
```

**(b)** Everything else is a mechanical rename: `MemoryModel::Instance().X(` and
`model.X(` both become `MemoryModel::X(`, and the `const MemoryModel &model = ...` locals go,
along with the blank line after them. Run from `rpibaremetalos/`:
```sh
sed -i \
  -e '/const MemoryModel &model = MemoryModel::Instance();/{N;s/^[^\n]*\n//;/^[[:space:]]*$/d}' \
  -e 's/MemoryModel::Instance()\./MemoryModel::/g' \
  -e 's/\bmodel\.\(AllocateUserFrame\|ReleaseUserFrame\|UserSpace\)(/MemoryModel::\1(/g' \
  src/c/platform/address_space.cpp \
  src/c/task/user_binary_loader.cpp \
  src/c/task/system_calls.cpp \
  src/c/task/task_impl.cpp \
  src/c/cli/test_command.cpp
```
This touches `AllocateTable`, `L3EntryFor`, `MapPages`, `Translate`, `TranslateForWrite` and
`~AddressSpace` in `address_space.cpp`, `LoadUserBinary`, `Malloc`, `MoveToUserSpace`, and
`test addrspace`. Afterwards
`grep -rn "MemoryModel::Instance\|model\.\(Alloc\|Release\|UserSpace\)" src` must print
nothing.

### 0.8 Boot banner, test headers and the obsolete setting

**(a)** `src/c/main.cpp`. Add the include after `#include "platform/mmu_manager.h"`:
```cpp
#include "platform/kernel_command_line.h"
```
**Before**
```cpp
    printf("Memory Model: %s\n", ToString(MMUManager::Instance().MemoryModel()));
```
**After**
```cpp
    //  memory_model= once chose between two models.  There is one now; say so rather than
    //      silently ignoring a stale cmdline.txt.

    {
        minstd::fixed_string<64> memory_model_setting;

        if (KernelCommandLine::FindSetting("memory_model", memory_model_setting))
        {
            printf("Ignoring obsolete kernel command line setting: memory_model\n");
        }
    }
```
> This is the first point where `printf` is known to work. `MMUManager::Initialize()` runs before
> the MMU and logging are up, which is why it can't report the setting itself.

**(b)** `src/c/cli/test_command.cpp`: the two test headers no longer name a model. The
regression script doesn't match on them. **Before**
```cpp
        context << minstd::format(buffer, "\nAddress space test [{}]\n",
                                  ToString(MMUManager::Instance().MemoryModel()));
```
**After**
```cpp
        context << "\nAddress space test\n";
```
**Before**
```cpp
        context << minstd::format(buffer, "\nUser task test [{}]\n",
                                  ToString(MMUManager::Instance().MemoryModel()));
```
**After**
```cpp
        context << "\nUser task test\n";
```
(`buffer` is still used further down in both functions.)

### 0.9 `rpibaremetalos/include/platform/address_space_layout.h`

**Before**
```c
//  User address space (per task, TTBR0, T0SZ=25).  IDENTICAL under both memory models.
//
//  The window sits at 256GB rather than at a natural low base because under
//      kernel_only_1_to_1 a task's TTBR0 ALSO carries the kernel identity map, which
//      occupies physical RAM and MMIO at their own addresses -- a 4MB user image would
//      collide with the identity mapping of physical 4MB.  256GB is L1 index 256, which
//      is zero on every board (only slots 0-3, and RPi5's 64, 65, 124-127, are ever
//      written).  AddressSpace::Initialize() asserts that at runtime.
```
**After**
```c
//  User address space (per task, TTBR0, T0SZ=25).
//
//  The window sits at 256GB (L1 index 256).  A task's L1 starts all zero, so nothing
//      requires it to sit that high; moving it lower is possible, but user.ld, hello.c and
//      the tests encode the address.
```

### 0.10 `rpibaremetalos/include/platform/address_space.h`

**(a)** **Before**
```cpp
    //  Allocate the L1, seed it per the memory model, take an ASID.
```
**After**
```cpp
    //  Allocate an all-zero L1, take an ASID.
```

**(b)** **Before**
```cpp
    //  The range check is what makes the syscall boundary MODEL-INDEPENDENT: under
    //      kernel_only_1_to_1 this table also contains the kernel identity map, and
    //      without the check a user pointer of 0x80000 would translate happily and hand
    //      the kernel its own image.  Range-check FIRST, walk second, always.
```
**After**
```cpp
    //  Range-check FIRST, walk second, always: the walk must never be asked about a VA
    //      outside the window.
```

**(c)** **Before**
```cpp
    //  What TTBR0 holds while a KERNEL task runs.  This is the whole of the runtime
    //      difference between the two memory models; Step 1B.2b already published it as
    //      __boot_ttbr0_base, so just return that.
```
**After**
```cpp
    //  What TTBR0 holds while a KERNEL task runs: MemoryModel::BootTTBR0Physical(), already
    //      published at boot as __boot_ttbr0_base.
```

### 0.11 `rpibaremetalos/src/c/platform/address_space.cpp` — comments only

**(a)** File header. **Before**
```cpp
//      l1_[UserSpace().l1_index].  They never write another L1 slot, in any model --
//      under kernel_only_1_to_1 the other slots are aliases of the kernel's own tables
//      and writing through them would corrupt every task at once.
```
**After**
```cpp
//      l1_[UserSpace().l1_index].  They never write another L1 slot.
```

**(b)** `Translate()`. **Before**
```cpp
    //  Range check FIRST, walk second, always -- see the header.  Under
    //      kernel_only_1_to_1 this table really does map the kernel identity range, and
    //      only this check keeps a user pointer of 0x80000 out.
```
**After**
```cpp
    //  Range check FIRST, walk second, always -- see the header.
```

**(c)** `~AddressSpace()`. **Before**
```cpp
    //  owned_ records only frames THIS space allocated -- its L1, its L2/L3 tables, and the
    //      blocks it mapped.  Under kernel_only_1_to_1 the L1 also contains kernel identity
    //      entries, but those were copied as values and their L2/L3 tables were never added
    //      to owned_, so teardown cannot free the kernel's own page tables.  Never walk the
    //      L1 to decide what to free; walk owned_.
```
**After**
```cpp
    //  owned_ records only frames THIS space allocated -- its L1, its L2/L3 tables, and the
    //      blocks it mapped.  Walk owned_ to decide what to free.
```
**(d)** `MapPages()` — **Before**
```cpp
    //      fault in both models.
```
**After**
```cpp
    //      fault.
```
(Phase B3 rewrites (a) and (c) again. Their **Before** blocks there match these **After** texts.)

### 0.12 Assembly comments

**(a)** `rpibaremetalos/src/asm/start.S`. **Before**
```asm
    //  Executing high now, so TTBR0 is free to become whatever the memory model chose --
    //      the kernel identity table under kernel_only_1_to_1, the all-zero table under
    //      kernel_high_user_low.  A published VALUE, not a branch: this code does not know
    //      the models exist.
```
**After**
```asm
    //  Executing high now, so TTBR0 can drop the boot identity map and take
    //      __boot_ttbr0_base: the all-zero __empty_user_page_table, published by core 0
    //      (MemoryModel::BootTTBR0Physical()).
```

**(b)** `rpibaremetalos/src/asm/global_variables.S`. **Before**
```asm
//  Physical base of the table core 0 decided TTBR0 should hold, per the memory model chosen
//      on the kernel command line: the kernel identity table under kernel_only_1_to_1, the
//      all-zero __empty_user_page_table under kernel_high_user_low.  Published as a VALUE so
//      that no assembly here has to know what a memory model is.  Same cache-line treatment
//      and same reason as __kernel_page_table_base directly above.
```
**After**
```asm
//  Physical base of the table TTBR0 holds at boot and while a kernel task runs: the all-zero
//      __empty_user_page_table (MemoryModel::BootTTBR0Physical()).  Same cache-line treatment
//      and same reason as __kernel_page_table_base directly above.
```

### 0.13 `rpibaremetalos/src/c/platform/exception_manager.cpp` — keep the debugging value

The identity model's one benefit was that an unconverted physical pointer "worked". Now it
faults, so name the likely cause in the fault log.

Add includes after `#include "task/task_impl.h"`:
```cpp
#include "sysregs.h"
#include "platform/address_space_layout.h"
```

**Before**
```cpp
void ExceptionManager::HandleException(unsigned int type, unsigned long esr, unsigned long address, unsigned long far)
{
    LogError("%s, Core: %d ESR: %016lx PC: %016lx FAR: %016lx\r\n", entry_error_messages[type], GetCoreID(), esr, address, far);
}
```
**After**
```cpp
void ExceptionManager::HandleException(unsigned int type, unsigned long esr, unsigned long address, unsigned long far)
{
    LogError("%s, Core: %d ESR: %016lx PC: %016lx FAR: %016lx\r\n", entry_error_messages[type], GetCoreID(), esr, address, far);

    //  TTBR0 holds no kernel mapping, so a kernel pointer never converted with
    //      PhysicalToKernelVirtualAddress() faults rather than silently working.  An EL1 data
    //      abort on a low-half VA below the user window is almost always exactly that (or a
    //      NULL-based pointer when FAR is tiny), so say so.

    const unsigned long ec = (esr >> ESR_ELx_EC_SHIFT) & 0x3F;

    if ((ec == ESR_ELx_EC_DABT_CUR) && (far < USER_SPACE_BASE))
    {
        LogError("  FAR %016lx is below the user window: a raw PHYSICAL address (missing "
                 "PhysicalToKernelVirtualAddress()?) or a NULL-based pointer\r\n", far);
    }
}
```
> Phase C later adds `ESR_ELx_EC_MASK` (0x3F) to `sysregs.h`. You may switch the literal to it
> then; this phase doesn't depend on Phase C.

### 0.14 Tests and test comments

**(a)** `rpibaremetalos/test/tools/qemu_regression_test.py`: drop the model dimension; the
suite runs twice per board.

Header comment. **Before**
```python
# Runs the suite four times for the requested machine -- both memory models
# (kernel_only_1_to_1 and kernel_high_user_low) each paired with the default
# (relaxed) alignment policy and strict_align=1 -- so regressions in any
# combination are caught. Board selection in the OS is runtime (MIDR_EL1
```
**After**
```python
# Runs the suite twice for the requested machine -- the default (relaxed)
# alignment policy and strict_align=1 -- so regressions in either are caught.
# The kernel has a single memory model, so the command line does not name
# one. Board selection in the OS is runtime (MIDR_EL1
```

`run()` signature and command line. **Before**
```python
        memory: str = '',
        memory_model: str = 'kernel_only_1_to_1',
        extra_cmdline: str = '') -> int:
```
**After**
```python
        memory: str = '',
        extra_cmdline: str = '') -> int:
```
**Before**
```python
        f' -append "console=ttys0,57600 host=qemu memory_model={memory_model}{extra_cmdline}"'
```
**After**
```python
        f' -append "console=ttys0,57600 host=qemu{extra_cmdline}"'
```

`main()` passes. **Before** (from the Pass 1 `print` to the end of `main`)
```python
    print(f'=== {args.machine} Pass 1: kernel_only_1_to_1, default alignment ===')
    result = run(args.qemu, args.armstub, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory,
                 memory_model='kernel_only_1_to_1')
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 2: kernel_only_1_to_1, strict_align=1 ===')
    result = run(args.qemu, args.armstub, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory,
                 memory_model='kernel_only_1_to_1',
                 extra_cmdline=' strict_align=1')
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 3: kernel_high_user_low, default alignment ===')
    result = run(args.qemu, args.armstub, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory,
                 memory_model='kernel_high_user_low')
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 4: kernel_high_user_low, strict_align=1 ===')
    return run(args.qemu, args.armstub, args.kernel, args.sdimage,
               machine=args.machine, memory=args.memory,
               memory_model='kernel_high_user_low',
               extra_cmdline=' strict_align=1')
```
**After**
```python
    print(f'=== {args.machine} Pass 1: default alignment ===')
    result = run(args.qemu, args.armstub, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory)
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 2: strict_align=1 ===')
    return run(args.qemu, args.armstub, args.kernel, args.sdimage,
               machine=args.machine, memory=args.memory,
               extra_cmdline=' strict_align=1')
```

**(b)** `rpibaremetalos/test/tools/qemu_cli_soak_test.py`. **Before**
```python
        f' -append "console=ttys0,57600 host=qemu memory_model=kernel_only_1_to_1"'
```
**After**
```python
        f' -append "console=ttys0,57600 host=qemu"'
```

**(c)** `rpibaremetalos/src/c/cli/test_command.cpp`: `test addrspace` header. **Before**
```cpp
    //  Model-independence check.  Under kernel_only_1_to_1 a task's L1 really does
    //      contain the kernel identity map, so rows 3 and 4 below are only "unmapped"
    //      because Translate() range-checks BEFORE it walks.  If either ever reports
    //      "mapped", the syscall boundary has a hole.
```
**After**
```cpp
    //  Translate() must range-check BEFORE it walks: rows 3 and 4 below (0x80000 and
    //      KERNEL_VA_BASE) must report "unmapped".  If either ever reports "mapped", the
    //      syscall boundary has a hole.
```
`test usertask` case 4. **Before**
```cpp
        //  Case 4 -- two tasks, concurrently, with different payloads.  Both must report
        //      "isolated": this is what proves the spaces are DISTINCT rather than merely
        //      permission-protected, and it has to hold under kernel_only_1_to_1 too.
```
**After**
```cpp
        //  Case 4 -- two tasks, concurrently, with different payloads.  Both must report
        //      "isolated": this is what proves the spaces are DISTINCT rather than merely
        //      permission-protected.
```

**(d)** `rpibaremetalos/user/hello.c`: header comment. **Before**
```c
//      1  -> plan case 1    read the kernel linear map        -> permission fault, both models
//      2  -> plan case 1b   read 0x80000, the kernel image PA -> translation fault under
//                                kernel_high_user_low, permission fault under
//                                kernel_only_1_to_1.  Different fault, same outcome: the
//                                task is killed.  This is the case that proves the
//                                compatibility model did not quietly become a hole.
//      3  -> plan case 2    write user text                   -> permission fault (RO)
//      4  -> plan case 3    execute from the stack            -> permission fault (UXN)
//      5  -> plan case 4    per-task heap isolation           -> must print "isolated"
//
//  Every case must produce the same outcome under BOTH memory models.  That is the whole
//      point of the design.
```
**After**
```c
//      1  -> plan case 1    read the kernel linear map        -> permission fault (EL1-only)
//      2  -> plan case 1b   read 0x80000, the kernel image PA -> translation fault: nothing
//                                is mapped there in a task's TTBR0
//      3  -> plan case 2    write user text                   -> permission fault (RO)
//      4  -> plan case 3    execute from the stack            -> permission fault (UXN)
//      5  -> plan case 4    per-task heap isolation           -> must print "isolated"
```
Case 5 comment. **Before**
```c
            //  Two tasks run this concurrently with different payloads.  Both must print
            //      "isolated": the heap page each one gets must be its own.  This is what
            //      proves the spaces are DISTINCT rather than merely permission-protected,
            //      and it must hold under kernel_only_1_to_1 too -- the two tasks have
            //      separate L1 tables there as well, differing only in the kernel slots.
```
**After**
```c
            //  Two tasks run this concurrently with different payloads.  Both must print
            //      "isolated": the heap page each one gets must be its own.  This is what
            //      proves the spaces are DISTINCT rather than merely permission-protected.
```

### 0.15 `rpibaremetalos/Makefile.aarch64.mk`

**(a)** Regression comment. **Before**
```make
#  Each board runs the suite four times -- both memory models
#  (kernel_only_1_to_1 and kernel_high_user_low) each paired with default
#  (relaxed) alignment and strict_align=1. RPi3 and RPi4 have materially
```
**After**
```make
#  Each board runs the suite twice -- default (relaxed) alignment and
#  strict_align=1. RPi3 and RPi4 have materially
```

**(b)** Interactive QEMU: drop `MM=`. **Before**
```make
#  Memory model: MM=1to1 (default) or MM=isolated
#  Strict alignment: SA=1 to enable
ifeq ($(MM),isolated)
QEMU_MEMORY_MODEL := kernel_high_user_low
else
QEMU_MEMORY_MODEL ?= kernel_only_1_to_1
endif

QEMU_STRICT_ALIGN := $(if $(filter 1,$(SA)), strict_align=1,)
```
**After**
```make
#  Strict alignment: SA=1 to enable
QEMU_STRICT_ALIGN := $(if $(filter 1,$(SA)), strict_align=1,)
```
**Before**
```make
	-append "console=ttys0,57600 host=qemu memory_model=$(QEMU_MEMORY_MODEL)$(QEMU_STRICT_ALIGN)"
```
**After**
```make
	-append "console=ttys0,57600 host=qemu$(QEMU_STRICT_ALIGN)"
```
> `QEMU_STRICT_ALIGN` already starts with a space (` strict_align=1`), so the result is
> well-formed with or without `SA=1`.

### 0.15a `rpibaremetalos/resources/cmdline.txt`

The shipped command line still names the setting. It's harmless now (ignored), but it's the
one file a new reader of the repo will see it in. **Before**
```
console=ttys0,57600 memory_model=kernel_high_user_low
```
**After**
```
console=ttys0,57600
```
Leave the file without a trailing newline, as it is today.

### 0.16 `AGENTS.md` (repo root)

The enum example names an enum that no longer exists. **Before**
```markdown
- **Enums** are declared `typedef enum class Name : uint32_t { ... } Name;` with **ALL_CAPS values** (e.g., `LogLevel::DEBUG_1`, `MemoryModelTypes::KERNEL_ONLY_1_TO_1`).
```
**After**
```markdown
- **Enums** are declared `typedef enum class Name : uint32_t { ... } Name;` with **ALL_CAPS values** (e.g., `LogLevel::DEBUG_1`, `TaskResultCodes::USER_BINARY_NOT_FOUND`).
```

### 0.17 Verify Phase 0 before moving on

1. **Nothing left behind.** Run from the repo root:
   ```sh
   test ! -e rpibaremetalos/src/c/platform/memory_model.cpp && \
   grep -rn "KERNEL_ONLY_1_TO_1\|kernel_only_1_to_1\|KernelOnly1To1\|KERNEL_HIGH_USER_LOW\|KernelHighUserLow\|MemoryModelTypes\|MemoryModel::Instance\|MemoryModel::Initialize\|\.MemoryModel()\|SeedTaskL1\|SharedUserSpaceLayout\|TASK_L1_ENTRY_COUNT\|QEMU_MEMORY_MODEL\|MM=" \
        --exclude-dir=build --exclude-dir=.git --exclude-dir=deps --exclude=DEMAND_PAGING_PLAN.md . \
        | grep -vi "retired"
   ```
   This must print nothing. Every surviving mention of the old model is a "retired" comment.
2. **Build:** `cd rpibaremetalos && make test && make`.
3. **Regression:** `make qemu-regression`. Each board runs **two** passes ("default alignment",
   "strict_align=1"), and every `test …` command must pass as before.
4. **Interactive boot:** `make qemu-rpi4`. The banner no longer has a `Memory Model:` line and
   doesn't print the obsolete-setting notice. `test addrspace` / `test usertask` pass.
5. **Obsolete setting is ignored:** temporarily put `memory_model=kernel_only_1_to_1` into the
   `-append` string of `QEMU_COMMON_FLAGS` and run `make qemu-rpi4`. It must boot normally and
   print `Ignoring obsolete kernel command line setting: memory_model`. Repeat with
   `memory_model=bogus`: same result. (Before Phase 0 a bogus value parked the core.) Revert the
   Makefile.
6. **Fault hint (optional, one-off):** temporarily add `*(volatile uint64_t *)0x80000;` to a CLI
   command, run it, and confirm the EL1 fault log includes the "raw PHYSICAL address" line.
   Revert.
7. **Real hardware:** boot each board once. A board whose `cmdline.txt` still has a
   `memory_model=` setting must boot normally and print the notice.

**Commit point:** Phase 0 is self-contained. Commit it on its own (on a feature branch, per
`AGENTS.md`) before starting Phase A.

---

## 3. Phase A — Geometry constants

### A1. `rpibaremetalos/include/os_memory_config.h`

**Before**
```c
#define TOTAL_RAM_IN_MB 1024
#define TOTAL_USER_SPACE_IN_MB 512
```
**After**
```c
#define TOTAL_RAM_IN_MB 1024

//  User-space geometry.  The window is a VIRTUAL budget -- every byte a task can address
//      (image, heap, stack) lives inside it -- but with demand paging untouched VA costs
//      nothing, so it is sized generously: 64GB = L1 slots 256..319.  Up to 256GB (slot 511)
//      needs no code change.  Sizes in MB; guard and stack are whole 1GB L1 slots.
#define TOTAL_USER_SPACE_IN_MB 65536  // 64GB window
#define USER_IMAGE_OFFSET_MB   4      // image_base = base + 4MB   (user.ld links here)
#define USER_HEAP_OFFSET_MB    256    // heap_base  = base + 256MB
#define USER_GUARD_SIZE_MB     1024   // 1GB unmapped gap between heap top and stack bottom
#define USER_MAX_STACK_SIZE_MB 1024   // 1GB per-task stack ceiling (demand-paged)
```

### A2. `rpibaremetalos/include/platform/address_space_layout.h`

**(a)** Fix the comment that still says one slot. **Before**
```c
//  The whole 512MB window lies inside the single 1GB span of L1 index 256, so one L1
//      slot per task is all the user side ever consumes.
```
**After**
```c
//  The 64GB window spans L1 indices 256..319.  A task's L1 starts all zero
//      (AddressSpace::Initialize()), which is what makes everything under them this task's
//      own.
```

**(b)** Replace the whole user-space constant block. **Before**
```c
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
```
**After**
```c
constexpr uint64_t USER_SPACE_BASE = 0x0000004000000000ULL;                                             //  256GB, L1 index 256

//  The window is 64GB: it SPANS L1 SLOTS 256..319 ([256GB, 320GB)).  It is a virtual budget
//      for everything the task can address; with demand paging, untouched VA costs nothing,
//      so it is sized generously.  Anything up to slot 511 (a 256GB window) needs only a new
//      TOTAL_USER_SPACE_IN_MB.
constexpr uint64_t USER_SPACE_L1_INDEX_START = USER_SPACE_BASE >> 30;                                   //  256
constexpr uint64_t USER_SPACE_TOP            = USER_SPACE_BASE + ((uint64_t)TOTAL_USER_SPACE_IN_MB * BYTES_1M);  //  base + 64GB = 320GB
constexpr uint64_t USER_SPACE_L1_INDEX_END   = (USER_SPACE_TOP - 1) >> 30;                             //  319

//  Regions, low to high.  image is fixed (the flat binary links here); heap grows UP to
//      heap_top; the guard gap is never mapped (a wild pointer faults instead of clobbering);
//      the stack grows DOWN from stack_top.
constexpr uint64_t USER_IMAGE_BASE   = USER_SPACE_BASE + ((uint64_t)USER_IMAGE_OFFSET_MB * BYTES_1M);   //  +4MB    header+text RO+X, then data+bss RW+XN
constexpr uint64_t USER_HEAP_BASE    = USER_SPACE_BASE + ((uint64_t)USER_HEAP_OFFSET_MB * BYTES_1M);    //  +256MB  RW+XN, grows up
constexpr uint64_t USER_STACK_TOP    = USER_SPACE_TOP;                                                  //  top of window
constexpr uint64_t USER_STACK_BOTTOM = USER_STACK_TOP - ((uint64_t)USER_MAX_STACK_SIZE_MB * BYTES_1M);  //  stack grows down to here
constexpr uint64_t USER_GUARD_BOTTOM = USER_STACK_BOTTOM - ((uint64_t)USER_GUARD_SIZE_MB * BYTES_1M);   //  [guard_bottom, stack_bottom) unmapped
constexpr uint64_t USER_HEAP_TOP     = USER_GUARD_BOTTOM;                                               //  exclusive heap ceiling

static_assert(USER_IMAGE_BASE < USER_HEAP_BASE,        "image must sit below the heap");
static_assert(USER_HEAP_BASE < USER_HEAP_TOP,          "heap region must be non-empty");
static_assert(USER_HEAP_TOP == USER_GUARD_BOTTOM,      "heap ceiling abuts the guard");
static_assert(USER_GUARD_BOTTOM < USER_STACK_BOTTOM,   "guard gap must be non-empty");
static_assert(USER_STACK_BOTTOM < USER_STACK_TOP,      "stack region must be non-empty");
static_assert((USER_SPACE_BASE & (BYTES_1G - 1)) == 0, "user window base must be 1GB-aligned");
static_assert(USER_SPACE_TOP <= (1ULL << 39),          "user window must fit the T0SZ=25 512GB TTBR0 range");
static_assert(USER_SPACE_L1_INDEX_END >= USER_SPACE_L1_INDEX_START, "L1 slot range sanity");
static_assert(USER_SPACE_L1_INDEX_END < 512,           "window must end inside the 512-entry L1");
```
> Notes:
> - The old `"user window must fit one L1 slot"` assert is **deliberately removed**, because the
>   window now spans 64 slots on purpose. `USER_SPACE_L1_INDEX` is gone too, so any leftover
>   user of it is a compile error, which is what we want.
> - The range assert is now `<=`, not `<`. `USER_SPACE_TOP` is exclusive, so a window ending
>   exactly at 512 GB (the 256 GB maximum) is legal. The old `<` would have rejected it.
> - The `< 512` assert uses a literal: 512 is the L1's entry count, and the only named
>   constant for it (`AddressSpace::ENTRIES_PER_TABLE`) is private to a header that includes
>   this one. The assert is redundant with the `1ULL << 39` check, but it states the real
>   invariant directly: the destructor indexes `l1_[slot]` for every slot in the range.

### A3. `rpibaremetalos/include/platform/memory_model.h`

Generalize the layout struct from one L1 slot to a slot range, and add the new region fields.

**(a)** The struct. **Before**
```cpp
struct UserSpaceLayout
{
    uint64_t space_base;        //  first VA of the user window
    uint64_t space_top;         //  exclusive
    uint64_t l1_index;          //  space_base >> 30
    uint64_t image_base;        //  header + text
    uint64_t heap_base;         //  sc_Malloc grows up from here
    uint64_t stack_top;         //  user stack grows down from here
};
```
**After**
```cpp
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
```

**(b)** `MemoryModel::UserSpace()` (as written in Phase 0, step 0.3). **Before**
```cpp
        static const UserSpaceLayout layout{
            USER_SPACE_BASE,
            USER_SPACE_TOP,
            USER_SPACE_L1_INDEX,
            USER_IMAGE_BASE,
            USER_HEAP_BASE,
            USER_STACK_TOP};
```
**After**
```cpp
        static const UserSpaceLayout layout{
            USER_SPACE_BASE,
            USER_SPACE_TOP,
            USER_SPACE_L1_INDEX_START,
            USER_SPACE_L1_INDEX_END,
            USER_IMAGE_BASE,
            USER_HEAP_BASE,
            USER_HEAP_TOP,
            USER_GUARD_BOTTOM,
            USER_STACK_BOTTOM,
            USER_STACK_TOP};
```

**Build checkpoint:** `cd rpibaremetalos && make` will now fail to compile `address_space.cpp`
and `task_impl.cpp` until Phases B and E land. That is expected, because the constants changed
shape. Continue.

---

## 4. Phase B — `AddressSpace`: multi-slot, fault-in, walk-based teardown

### B1. `rpibaremetalos/include/platform/platform_mmu.h`

Claim one of the software-reserved descriptor bits. The architecture ignores bits [58:55] for
translation (they are reserved for software use), so a mark there costs nothing. **Before**
```cpp
        uint64_t UXN : 1 = 0;                   // @54		Unprivileged Execute Never, set to 0
        uint64_t _reserved55_58 : 4 = 0;        // @55-58	Set to 0
```
**After**
```cpp
        uint64_t UXN : 1 = 0;                   // @54		Unprivileged Execute Never, set to 0
        uint64_t SoftwareDemandPaged : 1 = 0;   // @55		Software use (ignored by the MMU): leaf frame was faulted in by
                                                //          AddressSpace::FaultInUserPage and is freed by the teardown walk
        uint64_t _reserved56_58 : 3 = 0;        // @56-58	Software use, set to 0
```

### B2. `rpibaremetalos/include/platform/address_space.h`

**(a)** `OwnedBlock` keeps its shape and `MAX_OWNED_BLOCKS` stays at **512**: tables no longer
go into `owned_`, so it now holds only the L1 and the loader's text/data blocks. Update the
comments. **Before**
```cpp
    static constexpr uint32_t MAX_OWNED_BLOCKS = 512;           //  ~8KB of bookkeeping per task
```
**After**
```cpp
    //  owned_ holds the L1 and every MapPages block (today: image text + data).  L2/L3 table
    //      frames and demand-paged leaves are NOT recorded -- ~AddressSpace frees them by
    //      walking the window's L1 slots, so their count never limits a task.
    static constexpr uint32_t MAX_OWNED_BLOCKS = 512;           //  ~8KB of bookkeeping per task
```

**(b)** Declare the fault-in method. **Before**
```cpp
    bool MapPages(uint64_t user_va, uint64_t physical_base, uint64_t size,
                  Stage2AccessPermission ap, bool executable);

    //  Software walk of THIS space's tables.  Returns false if user_va is unmapped OR
```
**After**
```cpp
    bool MapPages(uint64_t user_va, uint64_t physical_base, uint64_t size,
                  Stage2AccessPermission ap, bool executable);

    //  Demand paging.  Maps ONE zeroed 4KB frame, RW+XN, at the page containing user_va when
    //      that page is UNMAPPED and lies in the live heap [heap_base, heap_break) or the stack
    //      [stack_bottom, stack_top).  Returns false for anything else -- the guard gap, heap
    //      above the break, an already-mapped page -- or when a frame/table cannot be
    //      allocated.  The leaf is marked SoftwareDemandPaged and is NOT added to owned_;
    //      ~AddressSpace frees it by walking the tables.
    //
    //  heap_break comes from the owning TaskImpl; the space itself does not track the break.
    bool FaultInUserPage(uint64_t user_va, uint64_t heap_break);

    //  Software walk of THIS space's tables.  Returns false if user_va is unmapped OR
```

**(c)** `owned_` member comment. **Before**
```cpp
    minstd::array<OwnedBlock, MAX_OWNED_BLOCKS> owned_;         //  table frames and mapped blocks alike
```
**After**
```cpp
    minstd::array<OwnedBlock, MAX_OWNED_BLOCKS> owned_;         //  the L1 and MapPages blocks; NOT L2/L3 tables or demand leaves
```

**(d)** `AllocateTable` comment. **Before**
```cpp
    //  Allocates and zeroes one 4KB table frame through MemoryModel::AllocateUserFrame,
    //      records it in owned_, and returns it as a kernel VA.  Null on failure or when
    //      owned_ is full.
```
**After**
```cpp
    //  Allocates and zeroes one 4KB table frame through MemoryModel::AllocateUserFrame and
    //      returns it as a kernel VA, or null on failure.  NOT recorded in owned_: every table hangs off one
    //      of the window's L1 slots and ~AddressSpace frees it by walking them.
```

### B3. `rpibaremetalos/src/c/platform/address_space.cpp`

**(a)** File header comment (as left by Phase 0, step 0.11a). **Before**
```cpp
//      L1 index = VA[38:30]   512 entries, 1GB each   -- only ONE is ever ours
//      L2 index = VA[29:21]   512 entries, 2MB each
//      L3 index = VA[20:12]   512 entries, 4KB each
//
//  MapPages() and Translate() touch only the L2/L3 chain hanging off
//      l1_[UserSpace().l1_index].  They never write another L1 slot.
```
**After**
```cpp
//      L1 index = VA[38:30]   512 entries, 1GB each   -- only [l1_index_start, l1_index_end] are ours
//      L2 index = VA[29:21]   512 entries, 2MB each
//      L3 index = VA[20:12]   512 entries, 4KB each
//
//  MapPages(), FaultInUserPage(), Translate() and the ~AddressSpace walk touch only the
//      L2/L3 chains hanging off the window's L1 slots.  They never read or write another L1
//      slot.
```

**(b)** `AllocateTable()` — stop recording table frames in `owned_`. **Before**
```cpp
uint64_t *AddressSpace::AllocateTable(uint64_t &physical_out)
{
    if (owned_count_ >= MAX_OWNED_BLOCKS)
    {
        return nullptr;
    }

    //  Through MemoryModel::AllocateUserFrame, never GetMemoryManager() directly, so a change
```
**After**
```cpp
uint64_t *AddressSpace::AllocateTable(uint64_t &physical_out)
{
    //  Through MemoryModel::AllocateUserFrame, never GetMemoryManager() directly, so a change
```
and **Before**
```cpp
    owned_[owned_count_++] = OwnedBlock{physical_out, PAGE_SIZE};

    return table;
}
```
**After**
```cpp
    return table;
}
```

**(c)** `Initialize()` — no change. Phase 0 (step 0.7a) made it zero the whole L1 itself,
so there's no per-slot check to widen. Keep `NextASID()` as its last step.

**(d)** `L3EntryFor(bool create)` — accept the whole slot range, not one slot. **Before**
```cpp
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    //  Everything we own hangs off exactly one L1 slot; refuse anything else outright so a
    //      bad VA can never reach another slot's descriptors.

    if (L1IndexOf(user_va) != layout.l1_index)
    {
        return nullptr;
    }

    uint64_t *l1_entry = &l1_[layout.l1_index];
```
**After**
```cpp
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    //  Everything we own hangs off the window's L1 slots; refuse anything else outright so a
    //      bad VA can never reach another slot's descriptors.

    const uint64_t l1_index = L1IndexOf(user_va);

    if ((l1_index < layout.l1_index_start) || (l1_index > layout.l1_index_end))
    {
        return nullptr;
    }

    uint64_t *l1_entry = &l1_[l1_index];
```

**(e)** `MapPages()` — refuse mappings that would overlap the guard gap. Insert right after the
existing overflow-safe range check. **Before**
```cpp
    if ((user_va < layout.image_base) ||
        (user_va > layout.space_top) ||
        (size > layout.space_top - user_va))
    {
        return false;
    }

    //  Refuse BEFORE mapping anything: on a full owned_ the block could not be recorded,
```
**After**
```cpp
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
```

**(f)** `MapPages()` — roll back on a mid-range failure. Leaving the descriptors written so far
would leave them pointing at a block the caller is about to release. **Before**
```cpp
        uint64_t *entry = L3EntryFor(user_va + offset, true);

        if (entry == nullptr)
        {
            return false;
        }
```
**After**
```cpp
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
```

**(g)** Add `FaultInUserPage`. Place it immediately **after** the end of `MapPages()` (before
`Translate`). Add `#include <string.h>` at the top of the file for `memset`.
```cpp
bool AddressSpace::FaultInUserPage(uint64_t user_va, uint64_t heap_break)
{
    const UserSpaceLayout &layout = MemoryModel::UserSpace();

    const uint64_t page_va = user_va & ~(PAGE_SIZE - 1);

    //  Only the LIVE heap and the stack are demand-paged.  The image is mapped eagerly at
    //      load; heap above the break, the guard gap and everything else must keep faulting.
    const bool in_heap  = (page_va >= layout.heap_base)    && (page_va < heap_break) && (heap_break <= layout.heap_top);
    const bool in_stack = (page_va >= layout.stack_bottom) && (page_va < layout.stack_top);

    if (!in_heap && !in_stack)
    {
        return false;
    }

    uint64_t *entry = L3EntryFor(page_va, true);

    if ((entry == nullptr) || (*entry != 0))     //  null: no table; nonzero: already mapped
    {
        return false;
    }

    MemoryPagePointer frame = MemoryModel::AllocateUserFrame(PAGE_SIZE);

    if (frame == 0)
    {
        return false;                            //  OOM: caller kills the task
    }

    //  GetFreeBlock does not clear memory.  Without this the previous owner's stack or heap
    //      contents would be readable by this task.

    memset((uint8_t *)frame, 0, PAGE_SIZE);

    VMSAv8_64_DESCRIPTOR page{};
    page.Raw64 = 0;
    page.EntryType = TableType::PAGE_TABLE;      //  level-3 page descriptor (bits[1:0] = 0b11)
    page.MemAttr = MemoryAttribute::NORMAL;
    page.S2AP = Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE;
    page.SH = Stage2Sharability::INNER_SHAREABLE;
    page.AF = AccessFlag::ACCESSED;              //  set now, or the retry takes an access-flag fault
    page.NonGlobalFlag = 1;                      //  per-ASID, not global
    page.Address = AddressField(frame.Physical());
    page.PXN = 1;                                //  EL1 never executes user pages
    page.UXN = 1;                                //  heap/stack are never executable
    page.SoftwareDemandPaged = 1;                //  ~AddressSpace frees this leaf on the walk

    *entry = page.Raw64;

    //  No TLBI: the entry was invalid, and invalid entries are never cached.  The dsb orders
    //      the zeroing and the descriptor before the retry; the eret back to EL0 is context
    //      synchronizing.

    asm volatile("dsb ish" ::: "memory");

    return true;
}
```

**(h)** `~AddressSpace()` — walk the window's slots, then free `owned_`. Replace the tail
(the comment as left by Phase 0, step 0.11c, and the loop as left by step 0.7b). **Before**
```cpp
    //  owned_ records only frames THIS space allocated -- its L1, its L2/L3 tables, and the
    //      blocks it mapped.  Walk owned_ to decide what to free.

    for (uint32_t i = 0; i < owned_count_; i++)
    {
        MemoryModel::ReleaseUserFrame(MemoryPagePointer{owned_[i].physical}, owned_[i].size);
    }

    owned_count_ = 0;
    l1_ = nullptr;
    l1_physical_ = 0;
}
```
**After**
```cpp
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
```
> The ASID block at the top of the destructor is unchanged. The walk doesn't depend on the ASID:
> `Initialize()` zeroes the L1 before it claims one, and `NextASID()` never fails (it parks).

**Why this is leak-free and double-free-free:**
- Demand leaves: they have `SoftwareDemandPaged`, so the walk frees each one as a single 4 KB frame.
- Image leaves: they don't have the bit, so the walk skips them. Their block is in `owned_` and is freed whole.
- L2/L3 tables: freed by the walk, and they are never in `owned_`.
- The L1 is in `owned_` and freed last, after the walk has finished reading it.
- After a failed `Initialize()` (the L1 allocation failed), `l1_` is null and `owned_` is
  empty, so nothing is freed.

---

## 5. Phase C — Fault classification + the demand-fault handler

### C1. `rpibaremetalos/include/sysregs.h`

Add the lower-EL abort classes and the DFSC decoding. **Keep** `ESR_ELx_EC_DABT_CUR`, because the
EL1 MMIO-probe path in `isr_process_management.S` uses it. **Before**
```c
#define ESR_ELx_EC_SHIFT		26
#define ESR_ELx_EC_SVC64		0x15    //  SVC instruction executed in AArch64 state
#define ESR_ELx_EC_DABT_CUR	0x25        //  Data Aborted Taken Without Change (current transaction)
```
**After**
```c
#define ESR_ELx_EC_SHIFT		26
#define ESR_ELx_EC_MASK			0x3F    //  EC[5:0], after shifting right by ESR_ELx_EC_SHIFT
#define ESR_ELx_EC_SVC64		0x15    //  SVC instruction executed in AArch64 state
#define ESR_ELx_EC_IABT_LOW		0x20    //  Instruction Abort from a LOWER exception level (EL0 -> EL1)
#define ESR_ELx_EC_DABT_LOW		0x24    //  Data Abort from a LOWER exception level (EL0 -> EL1)
#define ESR_ELx_EC_DABT_CUR		0x25    //  Data Abort taken without a change in exception level (EL1 -> EL1)

//  Data Abort ISS fields (EC == DABT_*).
//
//  DFSC = ISS[5:0].  0b0001LL (0x04-0x07) is "translation fault, level LL" -- the signature
//      of an unmapped page, which is exactly what demand paging recovers from.  NOTE 0x00-0x03
//      are ADDRESS SIZE faults, not translation faults.  Anything else (access flag 0x08-0x0B,
//      permission 0x0C-0x0F, external abort, alignment) is a real fault and kills the task.
//  FnV = ISS[10]: when set, FAR_EL1 is not valid and the fault cannot be resolved.

#define ESR_ELx_DFSC_MASK		0x3F
#define ESR_ELx_DFSC_IS_TRANSLATION(dfsc)	(((dfsc) & 0x3C) == 0x04)
#define ESR_ELx_FnV				(1UL << 10)
```
> `sysregs.h` is also included by `.S` files. That's harmless: the `UL` suffix and the
> function-like macro only expand where they are used, and only C++ uses them. Don't reference
> `ESR_ELx_FnV` from assembly.

### C2. NEW FILE `rpibaremetalos/include/platform/page_fault_handler.h`

```cpp
// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Demand paging entry point, called from EL0_SynchronousException in
//      isr_process_management.S for every EL0 synchronous exception that is not an SVC.
//
//  Returns 1 if the fault was a recoverable unmapped heap/stack page (the page has been
//      mapped and the caller must eret to re-run the faulting instruction), or 0 if it was
//      not a demand-paging fault (the caller treats it as a fatal user fault).
//
//  Runs on the faulting task's kernel stack, on the frame KernelEntry EL0 built -- the same
//      frame and context a syscall runs in, so it may allocate exactly as sc_Malloc does.

extern "C" int TryHandleDemandPageFault(unsigned long esr, unsigned long elr, unsigned long far);
```
> C++ only. The `.S` file does **not** include this header: `bl TryHandleDemandPageFault` is
> resolved at link time.

### C3. NEW FILE `rpibaremetalos/src/c/platform/page_fault_handler.cpp`

```cpp
// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/page_fault_handler.h"

#include "sysregs.h"

#include "task/task_impl.h"

extern "C" int TryHandleDemandPageFault(unsigned long esr, unsigned long elr, unsigned long far)
{
    (void)elr;    //  the retry uses ELR_EL1 implicitly via eret; we do not step the PC

    const uint32_t ec = (uint32_t)((esr >> ESR_ELx_EC_SHIFT) & ESR_ELx_EC_MASK);

    //  Only a DATA abort from EL0 can be a demand-paging event.  An instruction fetch from the
    //      user window can only ever be the (already-mapped) image text -- heap and stack are
    //      UXN -- so an IABT is always a genuine fault, not a page-in.
    if (ec != ESR_ELx_EC_DABT_LOW)
    {
        return 0;
    }

    if ((esr & ESR_ELx_FnV) != 0)
    {
        return 0;                            //  FAR not valid: nothing to page in
    }

    if (!ESR_ELx_DFSC_IS_TRANSLATION(esr & ESR_ELx_DFSC_MASK))
    {
        return 0;                            //  permission / access-flag / abort: not recoverable
    }

    //  TaskImpl::FaultInUserPage supplies the heap break and returns false for anything that
    //      is not an unmapped live-heap or stack page (or on OOM).  On false the assembly falls
    //      through to HandleUserTaskFault and the task is killed.
    return task::TaskImpl::GetTask().FaultInUserPage(far) ? 1 : 0;
}
```
> No Makefile change needed: `src/c/platform/*.cpp` is already wildcarded into `CPP_SRC`
> (see `Makefile.aarch64.mk` `CPP_DIRS`).

---

## 6. Phase D — Route the EL0 fault through the handler

### D1. `rpibaremetalos/src/asm/isr_process_management.S`

Replace the tail of `EL0_SynchronousException` (the "genuine fault → kill" block) with a
demand-fault attempt first. **Before**
```asm
	cmp		x24, #ESR_ELx_EC_SVC64								//	If the exception is from SVC in 64-bit state, then
	b.eq	EL0_SyscallDispatcher								//		branch to the syscall dispatcher

	//	Not a syscall, so a genuine fault taken at EL0.  Kill the TASK, not the core: an
	//		EL1 fault is fatal, but a user program faulting is an ordinary event.  The
	//		frame KernelEntry EL0 just built is the SAME frame a syscall runs on, which is
	//		why HandleUserTaskFault can simply call Exit() the way sc_Exit does.
	//		HandleUserTaskFault never returns.

	mrs		x0, esr_el1
	mrs		x1, elr_el1
	mrs		x2, far_el1
	bl		HandleUserTaskFault

	HandleInvalidEntryNoFrame EL0, SYNC_ERROR								//	unreachable -- Exit() does not return
```
**After**
```asm
	cmp		x24, #ESR_ELx_EC_SVC64								//	If the exception is from SVC in 64-bit state, then
	b.eq	EL0_SyscallDispatcher								//		branch to the syscall dispatcher

	//	Not a syscall.  Before treating it as fatal, try demand paging: an unmapped heap or
	//		stack page is a normal, recoverable event.  The C handler returns 1 only when it
	//		has mapped the page; then KernelExit EL0 restores the full CPU state (including the
	//		faulting PC saved in the frame's ELR slot) and eret re-runs the faulting instruction,
	//		which now succeeds.
	//		It returns 0 for anything else and we fall through to the kill-the-task path.
	//
	//	ESR/ELR/FAR are kept in x19-x21 so they survive the C call for the kill path.  These
	//		are callee-saved, and KernelEntry EL0 already saved the user's values to the frame
	//		that KernelExit EL0 restores, so using them here is free.

	mrs		x19, esr_el1
	mrs		x20, elr_el1
	mrs		x21, far_el1

	mov		x0, x19
	mov		x1, x20
	mov		x2, x21
	bl		TryHandleDemandPageFault
	cbz		w0, EL0_UserFault									//	int return: only w0 is defined (AAPCS64)

	KernelExit EL0												//	page faulted in: retry the instruction

EL0_UserFault:

	//	A genuine fault taken at EL0.  Kill the TASK, not the core: an EL1 fault is fatal,
	//		but a user program faulting is an ordinary event.  The frame KernelEntry EL0 built
	//		is the SAME frame a syscall runs on, which is why HandleUserTaskFault can simply
	//		call Exit() the way sc_Exit does.  HandleUserTaskFault never returns.

	mov		x0, x19
	mov		x1, x20
	mov		x2, x21
	bl		HandleUserTaskFault

	HandleInvalidEntryNoFrame EL0, SYNC_ERROR								//	unreachable -- Exit() does not return
```
> **Why no extra save/restore:** `KernelEntry EL0` saved x0–x30, sp_el0, ELR_EL1 and SPSR_EL1
> to the frame (it `stp`s x18–x21 at `[sp, #16*9]`/`[sp, #16*10]`). `KernelExit EL0` reloads
> every GPR, and reloads ELR_EL1 **from the frame** (`[sp, #16*16]`) before `eret`. Nothing
> touches that slot on this path, so `eret` re-executes the exact faulting instruction against
> the newly mapped page. (The EL1 MMIO-probe path deliberately adds 4 to that slot; this path
> must not.) The two bugs this avoids:
> `cbz x0` would test undefined upper bits of an `int`, and re-using x0–x2 after the call
> would hand `HandleUserTaskFault` garbage for its log line.

**Build checkpoint:** `cd rpibaremetalos && make` still fails until Phase E adds
`TaskImpl::FaultInUserPage`. Continue.

---

## 7. Phase E — Task heap/stack become lazy; syscalls fault in too

### E1. `rpibaremetalos/include/task/task_impl.h`

Replace the `MapIntoUserHeap` declaration. **Before**
```cpp
        //  Maps size bytes of physical memory at the current user heap break and returns the
        //      USER VA it landed at, or 0 if it will not fit.  Implemented in Step 2.8.

        uint64_t MapIntoUserHeap(uint64_t physical, uint64_t size);
```
**After**
```cpp
        //  Reserve `size` bytes of user heap at the current break and return the USER VA, or
        //      0 if it will not fit below the guard gap.  Nothing is mapped: each page is
        //      faulted in on first touch (demand paging).

        uint64_t GrowUserHeap(uint64_t size);

        //  Demand-page the page containing user_va if it is an unmapped live-heap or stack
        //      page.  The ONE entry point for both first-touch paths: the EL0 fault handler
        //      and the kernel's user_access copies.  False for a kernel task.

        bool FaultInUserPage(uint64_t user_va);
```

### E2. `rpibaremetalos/src/c/task/task_impl.cpp`

**(a)** Replace the `MapIntoUserHeap` definition. **Before**
```cpp
    uint64_t TaskImpl::MapIntoUserHeap(uint64_t physical, uint64_t size)
    {
        if ((address_space_ == nullptr) || (size == 0))
        {
            return 0;
        }

        const uint64_t rounded_size = (size + BYTES_4K - 1) & ~(BYTES_4K - 1);
        const uint64_t heap_limit = USER_STACK_TOP - stack_size_in_bytes_;

        if ((user_heap_break_ >= heap_limit) || (rounded_size > heap_limit - user_heap_break_))
        {
            return 0;
        }

        if (!address_space_->MapPages(user_heap_break_, physical, rounded_size,
                                      Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false))
        {
            return 0;
        }

        const uint64_t allocated_at = user_heap_break_;

        user_heap_break_ += rounded_size;

        return allocated_at;
    }
```
**After**
```cpp
    uint64_t TaskImpl::GrowUserHeap(uint64_t size)
    {
        if ((address_space_ == nullptr) || (size == 0))
        {
            return 0;
        }

        const uint64_t rounded_size = (size + BYTES_4K - 1) & ~(BYTES_4K - 1);

        if (rounded_size < size)
        {
            return 0;                                       //  size near 2^64 wrapped to 0
        }

        //  The heap grows up to the guard gap, never into it: the gap is what turns a runaway
        //      heap into a fault instead of silent stack corruption.

        const uint64_t heap_limit = MemoryModel::UserSpace().heap_top;

        if ((user_heap_break_ >= heap_limit) || (rounded_size > heap_limit - user_heap_break_))
        {
            return 0;
        }

        //  Reserve only.  The first touch of each page -- by the task or by a syscall copying
        //      through the pointer -- faults it in, so an untouched allocation costs nothing.

        const uint64_t allocated_at = user_heap_break_;

        user_heap_break_ += rounded_size;

        return allocated_at;
    }

    bool TaskImpl::FaultInUserPage(uint64_t user_va)
    {
        return (address_space_ != nullptr) && address_space_->FaultInUserPage(user_va, user_heap_break_);
    }
```

**(b)** In `MoveToUserSpace`, stop mapping the user stack. **Before**
```cpp
        //  USER stack: a fresh block mapped RW+XN just below USER_STACK_TOP.  The KERNEL
        //      stack this task is executing on right now (stack_) is untouched -- it is
        //      SP_EL1 for the task's syscalls and interrupts.

        MemoryPagePointer user_stack = MemoryModel::AllocateUserFrame(stack_size_in_bytes_);

        if (user_stack == 0)
        {
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK;
        }

        if (!address_space_->MapPages(USER_STACK_TOP - stack_size_in_bytes_, user_stack.Physical(),
                                      stack_size_in_bytes_,
                                      Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false))
        {
            //  MapPages failed, so the space never took ownership -- give the stack back here.

            MemoryModel::ReleaseUserFrame(user_stack, stack_size_in_bytes_);
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK;
        }

        regs.sp = (void *)USER_STACK_TOP;                       //  KernelExit EL0 restores this into SP_EL0
```
**After**
```cpp
        //  USER stack: nothing is mapped.  SP starts at USER_STACK_TOP and the first push
        //      faults in the top page; FaultInUserPage covers [USER_STACK_BOTTOM, USER_STACK_TOP),
        //      and below that is the guard gap.  stack_size_in_bytes_ sizes the KERNEL stack
        //      (stack_) only -- SP_EL1 for this task's syscalls and interrupts.

        regs.sp = (void *)USER_STACK_TOP;                       //  KernelExit EL0 restores this into SP_EL0
```
> `UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK` is still used by `task_manager_impl.cpp`
> for kernel stacks; leave the enum alone.

### E3. `rpibaremetalos/src/c/task/system_calls.cpp`

Collapse `Malloc` to call the new method (as left by Phase 0, step 0.7b). **Before**
```cpp
    unsigned long Malloc( unsigned long block_size )
    {
        task::TaskImpl &task = task::TaskImpl::GetTask();

        if (task.UserAddressSpace() == nullptr)
        {
            return (unsigned long)-1;                           //  kernel task: no user heap
        }

        //  Through MemoryModel::AllocateUserFrame, never GetMemoryManager() directly, so a
        //      change to how user frames are sourced touches no call site.

        MemoryPagePointer frame = MemoryModel::AllocateUserFrame(block_size);

        if (frame == 0)
        {
            return (unsigned long)-1;
        }

        uint64_t user_va = task.MapIntoUserHeap(frame.Physical(), block_size);

        if (user_va == 0)
        {
            MemoryModel::ReleaseUserFrame(frame, block_size);
            return (unsigned long)-1;
        }

        return user_va;
    }
```
**After**
```cpp
    unsigned long Malloc( unsigned long block_size )
    {
        task::TaskImpl &task = task::TaskImpl::GetTask();

        if (task.UserAddressSpace() == nullptr)
        {
            return (unsigned long)-1;                           //  kernel task: no user heap
        }

        //  Reserve VA only.  Frames still come through MemoryModel::AllocateUserFrame --
        //      at first touch, in AddressSpace::FaultInUserPage.

        uint64_t user_va = task.GrowUserHeap(block_size);

        return user_va ? user_va : (unsigned long)-1;
    }
```
> **Leave** `#include "platform/memory_model.h"` in place. `Malloc` no longer needs it, but
> `CloneTask`'s `MemoryPagePointer` parameter may reach this file through it (`system_calls.h`
> doesn't include `memory_manager.h`). `MapIntoUserHeap` had exactly this one caller.

### E4. `rpibaremetalos/src/c/task/user_access.cpp`

The kernel never dereferences a user VA. It translates through `AddressSpace::Translate` /
`TranslateForWrite`, and those return false for an unmapped page. Under demand paging an
untouched heap buffer, or a stack page below the deepest one touched so far, is unmapped. So
without this step `sc_Write` from a fresh `sc_Malloc` buffer, or a result pointer into a new
heap block (`CloneTask`), would fail. The kernel's first touch is as legitimate as the user's,
so it must fault the page in the same way.

**(a)** Add two helpers to the anonymous namespace, after `KernelWindow`. **Before**
```cpp
        uint8_t *KernelWindow(uint64_t physical, uint64_t offset_in_page)
        {
            return (uint8_t *)(PhysicalToKernelVirtualAddress(physical) + offset_in_page);
        }
    }
```
**After**
```cpp
        uint8_t *KernelWindow(uint64_t physical, uint64_t offset_in_page)
        {
            return (uint8_t *)(PhysicalToKernelVirtualAddress(physical) + offset_in_page);
        }

        //  Demand paging: an untouched heap or stack page is UNMAPPED, and the kernel's first
        //      touch through a syscall pointer is as legitimate as the task's own.  On a miss,
        //      fault the page in exactly as the EL0 handler would, then translate again.
        //      Whatever FaultInUserPage refuses (guard gap, above the break, read-only image,
        //      OOM) stays a failure.

        bool TranslateOrFaultIn(const AddressSpace &space, uint64_t page_base, uint64_t &physical)
        {
            return space.Translate(page_base, physical) ||
                   (TaskImpl::GetTask().FaultInUserPage(page_base) && space.Translate(page_base, physical));
        }

        bool TranslateForWriteOrFaultIn(const AddressSpace &space, uint64_t page_base, uint64_t &physical)
        {
            return space.TranslateForWrite(page_base, physical) ||
                   (TaskImpl::GetTask().FaultInUserPage(page_base) && space.TranslateForWrite(page_base, physical));
        }
    }
```

**(b)** Route every translate through them. There are four call sites, all with the same
pattern:

| Function | Before | After |
|---|---|---|
| `CopyToKernelSpaceFromUserSpace` | `if (!space->Translate(span.page_base, physical))` | `if (!TranslateOrFaultIn(*space, span.page_base, physical))` |
| `CopyToUserSpaceFromKernelSpace` (PASS 1) | `if (!space->TranslateForWrite(span.page_base, physical))` | `if (!TranslateForWriteOrFaultIn(*space, span.page_base, physical))` |
| `CopyToUserSpaceFromKernelSpace` (PASS 2) | `if (!space->TranslateForWrite(span.page_base, physical))` | unchanged — PASS 1 already mapped every page |
| `CopyStringFromUserSpaceToKernelSpace` | `if (!space->Translate(span.page_base, physical))` | `if (!TranslateOrFaultIn(*space, span.page_base, physical))` |

> PASS 1 of `CopyToUserSpaceFromKernelSpace` can now map pages and then fail on a later page.
> That doesn't break its "leave user memory exactly as found" contract: a freshly faulted page
> is zero-filled, which is precisely what the task would have read from it anyway.

**Build checkpoint:** `cd rpibaremetalos && make` — the full aarch64 image should build and link.

---

## 8. Phase F — Tests

Each case below has exactly one expected outcome. With a single memory model, the suite's old
"same outcome under both models" constraint no longer applies. Every case runs in both
regression passes (default alignment and `strict_align=1`).

### F1. `rpibaremetalos/user/hello.c`

**(a)** Layout constants. `hello.c` already hardcodes `USER_IMAGE_BASE`; add the rest.
**Before**
```c
//  Must match USER_IMAGE_BASE in include/platform/address_space_layout.h.

#define USER_IMAGE_BASE 0x0000004000400000UL
#define KERNEL_VA_BASE  0xFFFFFF8000000000UL
```
**After**
```c
//  Must match include/platform/address_space_layout.h (and os_memory_config.h).

#define USER_SPACE_BASE   0x0000004000000000UL
#define USER_IMAGE_BASE   (USER_SPACE_BASE + (4UL    << 20))
#define USER_GUARD_BOTTOM (USER_SPACE_BASE + (62UL << 30))     //  base + 62GB (64GB window, 1GB guard, 1GB stack)
#define KERNEL_VA_BASE    0xFFFFFF8000000000UL

//  1MB of stack, touched one page at a time from the top down: 256 demand faults.

static __attribute__((noinline)) int TouchDeepStack(void)
{
    volatile unsigned char frame[1024 * 1024];

    for (unsigned long i = 0; i < 256; i++)
    {
        frame[sizeof(frame) - 1 - (i * 4096)] = (unsigned char)i;
    }

    for (unsigned long i = 0; i < 256; i++)
    {
        if (frame[sizeof(frame) - 1 - (i * 4096)] != (unsigned char)i)
        {
            return 0;
        }
    }

    return 1;
}
```
Also extend the case table in the file's top comment with cases 7–11. Note that the existing
cases print shifted labels (arg 6 prints "case 5"), but the new cases print their own arg
number: arg 7 prints "case 7".

**(b)** Add the cases after `case 6`. **Before** (the `case 6` block ends here)
```c
            sc_Write("case 5: NOT KILLED\n");
            break;
        }

        default:
```
**After**
```c
            sc_Write("case 5: NOT KILLED\n");
            break;
        }

        case 7:
        {
            //  Demand paging at scale.  Allocate 64 heap pages and touch each one for the
            //      first time (a translation fault), then read it back.  If any page-in failed
            //      the store would fault fatally and the OK line would never print.

            sc_Write("case 7: demand paging\n");

            int ok = 1;

            for (unsigned long i = 0; i < 64; i++)
            {
                unsigned long p = sc_Malloc(4096);

                if (p == (unsigned long)-1)
                {
                    ok = 0;
                    break;
                }

                volatile uint64_t *q = (volatile uint64_t *)p;

                if (*q != 0)                        //  first touch is a READ: must be a zeroed frame
                {
                    ok = 0;
                    break;
                }

                *q = 0x1000UL + i;

                if (*q != (0x1000UL + i))
                {
                    ok = 0;
                    break;
                }
            }

            sc_Write(ok ? "case 7: demand paging OK\n" : "case 7: demand paging FAILED\n");
            break;
        }

        case 8:
        {
            //  One page past the heap break is a wild pointer, not a page-in.

            sc_Write("case 8: writing above the heap break\n");
            unsigned long p = sc_Malloc(4096);

            if (p == (unsigned long)-1)
            {
                sc_Write("case 8: MALLOC FAILED\n");    //  otherwise *(-1) would "pass" by dying
                break;
            }

            *(volatile uint64_t *)p = 1;            //  inside the break: faults in, survives
            *(volatile uint64_t *)(p + 4096) = 1;   //  at the break: must be killed
            sc_Write("case 8: NOT KILLED\n");
            break;
        }

        case 9:
        {
            sc_Write("case 9: writing into the guard gap\n");
            *(volatile uint64_t *)USER_GUARD_BOTTOM = 1;
            sc_Write("case 9: NOT KILLED\n");
            break;
        }

        case 10:
        {
            sc_Write(TouchDeepStack() ? "case 10: deep stack OK\n" : "case 10: deep stack FAILED\n");
            break;
        }

        case 11:
        {
            //  The KERNEL is the first to touch a page.  The message sits at the very end of
            //      page one and its terminator is the first byte of page two, which nothing has
            //      touched.  sc_Write's string copy must fault page two in (zero-filled) to
            //      find the '\0'; otherwise the copy fails and the line silently never prints.

            static const char message[] = "case 11: kernel first touch OK\n";
            const unsigned long length = sizeof(message) - 1;

            unsigned long p = sc_Malloc(8192);

            if (p == (unsigned long)-1)
            {
                sc_Write("case 11: MALLOC FAILED\n");
                break;
            }

            char *s = (char *)(p + 4096 - length);

            for (unsigned long i = 0; i < length; i++)
            {
                s[i] = message[i];
            }

            sc_Write(s);
            break;
        }

        default:
```

### F2. `rpibaremetalos/src/c/cli/test_command.cpp`

**(a)** Register the new cases in the `test usertask` table. **Before**
```cpp
        const UserTaskCase cases[] = {
            {"case 0 (hello)",       0, "runs to completion, prints 'hello from EL0'"},
            {"case 1 (kernel VA)",   1, "KILLED -- permission fault reading the kernel linear map"},
            {"case 1b (0x80000)",    2, "KILLED -- fault reading the kernel image PA"},
            {"case 2 (write text)",  3, "KILLED -- permission fault writing read-only user text"},
            {"case 3 (exec stack)",  4, "KILLED -- permission fault executing from a UXN stack"},
            {"case 5 (bad syscall)", 6, "KILLED -- out-of-range syscall number 99, core survives"},
        };
```
**After**
```cpp
        const UserTaskCase cases[] = {
            {"case 0 (hello)",             0, "runs to completion, prints 'hello from EL0'"},
            {"case 1 (kernel VA)",         1, "KILLED -- permission fault reading the kernel linear map"},
            {"case 1b (0x80000)",          2, "KILLED -- fault reading the kernel image PA"},
            {"case 2 (write text)",        3, "KILLED -- permission fault writing read-only user text"},
            {"case 3 (exec stack)",        4, "KILLED -- permission fault executing from a UXN stack"},
            {"case 5 (bad syscall)",       6, "KILLED -- out-of-range syscall number 99, core survives"},
            {"case 7 (demand paging)",     7, "mallocs + touches 64 heap pages, each faulted in zeroed"},
            {"case 8 (above heap break)",  8, "KILLED -- translation fault one page past the heap break"},
            {"case 9 (guard gap)",         9, "KILLED -- translation fault in the heap/stack guard gap"},
            {"case 10 (deep stack)",      10, "touches 1MB of stack, 256 pages faulted in"},
            {"case 11 (kernel 1st touch)",11, "sc_Write's copy faults in an untouched heap page"},
        };
```

**(b)** Kernel-side unit check of `FaultInUserPage` in `test addrspace`. This is deterministic and
needs no EL0 task. Insert it after the `checks` loop. **Before**
```cpp
            context << minstd::format(buffer, "  {}: {}\n", check.label,
                                      mapped ? "mapped" : "unmapped");
        }

        //  Step 2.6 -- load hello.bin into a scratch space and confirm the two-region map.
```
**After**
```cpp
            context << minstd::format(buffer, "  {}: {}\n", check.label,
                                      mapped ? "mapped" : "unmapped");
        }

        //  Demand paging: FaultInUserPage maps a live-heap page and a stack page, and nothing
        //      else.  The space is then destroyed, which exercises the teardown walk.

        {
            const uint64_t heap_break = USER_HEAP_BASE + BYTES_4K;

            //  Order matters: the "already faulted" row relies on the first row having run.

            const Expectation faults[] = {
                {"fault-in heap below the break",    USER_HEAP_BASE,      true},
                {"fault-in already-faulted page",    USER_HEAP_BASE,      false},
                {"fault-in heap AT the break",       heap_break,          false},
                {"fault-in top stack page",          USER_STACK_TOP - 8,  true},
                {"fault-in stack bottom page",       USER_STACK_BOTTOM,   true},
                {"fault-in guard gap (top page)",    USER_STACK_BOTTOM - 8, false},
                {"fault-in guard gap (bottom page)", USER_GUARD_BOTTOM,   false},
                {"fault-in image page",              USER_IMAGE_BASE,     false},
            };

            for (const Expectation &fault : faults)
            {
                uint64_t physical = 0;
                const bool mapped = space->FaultInUserPage(fault.va, heap_break) &&
                                    space->Translate(fault.va, physical);

                if (mapped != fault.expect_mapped)
                {
                    context << minstd::format(buffer, "FAIL: {} -- expected {}, got {}\n",
                                              fault.label,
                                              fault.expect_mapped ? "mapped" : "refused",
                                              mapped ? "mapped" : "refused");
                    failures++;
                    continue;
                }

                context << minstd::format(buffer, "  {}: {}\n", fault.label,
                                          mapped ? "mapped" : "refused");
            }
        }

        //  Page accounting -- proves the teardown walk is leak-free and covers the WHOLE slot
        //      range.  A fresh space that faults in one heap page (L1 slot 256, the first) and
        //      one stack page (L1 slot 319, the last) must take exactly 7 pages -- L1, 2 x L2, 2 x L3, 2 leaves -- and
        //      return every one on destruction.  The AddressSpace object itself comes from the
        //      kernel heap, not the page allocator, so it is allocated BEFORE the snapshot.
        //      Assumes nothing else allocates pages meanwhile, which holds on an idle CLI.

        {
            const uint64_t expected_pages = (7 * BYTES_4K) / GetMemoryManager().PageSize();

            AddressSpace *accounting_space = dynamic_new<AddressSpace>().release();

            if (accounting_space == nullptr)
            {
                context << "FAIL: could not allocate the accounting AddressSpace\n";
                failures++;
            }
            else
            {
                const uint64_t free_before = GetMemoryManager().FreePages();

                const bool faulted = accounting_space->Initialize() &&
                                     accounting_space->FaultInUserPage(USER_HEAP_BASE, USER_HEAP_BASE + BYTES_4K) &&
                                     accounting_space->FaultInUserPage(USER_STACK_TOP - 8, USER_HEAP_BASE + BYTES_4K);

                const uint64_t free_during = GetMemoryManager().FreePages();

                dynamic_delete(accounting_space);

                const uint64_t free_after = GetMemoryManager().FreePages();

                if (!faulted || ((free_before - free_during) != expected_pages) || (free_after != free_before))
                {
                    context << minstd::format(buffer, "FAIL: page accounting -- faulted {}, used {} (expected {}), leaked {}\n",
                                              faulted, free_before - free_during, expected_pages,
                                              (int64_t)(free_before - free_after));
                    failures++;
                }
                else
                {
                    context << minstd::format(buffer, "  page accounting: {} pages used, all returned\n", expected_pages);
                }
            }
        }

        //  Step 2.6 -- load hello.bin into a scratch space and confirm the two-region map.
```
> If `minstd::format` can't format `bool` or `int64_t`, cast to `uint32_t`/`uint64_t`. If the
> accounting check proves flaky because another task allocates pages concurrently, keep the
> `free_after == free_before` leak check and drop the exact `expected_pages` comparison.
> The rest of `test addrspace` needs no change. `USER_IMAGE_BASE` and `USER_HEAP_BASE` keep
> their meaning, the loader still leaves the heap unmapped, and `Initialize()` now validates
> all 64 window slots (always zero, since a task's L1 is seeded all-zero).

### F3. `rpibaremetalos/test/tools/qemu_regression_test.py`

Assert that the positive cases complete. Cases 8 and 9 are already covered by the existing
`'NOT KILLED'` scan. **Before**
```python
        if output.count('case 4: isolated') != 2:
            print('\nFAIL [test usertask]: case 4 did not run both tasks')
            failures += 1

        for bad in ('Failed to move task to user space', 'NOT KILLED'):
```
**After**
```python
        if output.count('case 4: isolated') != 2:
            print('\nFAIL [test usertask]: case 4 did not run both tasks')
            failures += 1

        # The demand-paging cases must report OK in every pass.
        # Cases 8 and 9 (wild heap pointer, guard gap) are caught by the 'NOT KILLED' scan below.
        for good in ('case 7: demand paging OK',
                     'case 10: deep stack OK',
                     'case 11: kernel first touch OK'):
            if good not in output:
                print(f'\nFAIL [test usertask]: missing "{good}"')
                failures += 1

        # Exactly seven cases must be killed: args 1, 2, 3, 4, 6, 8, 9. Fewer means a negative
        # case survived some other way; more means a positive case (0, 5 x2, 7, 10, 11) died,
        # e.g. because a page-in was refused.
        kills = output.count('Exception in User Task - task killed')
        if kills != 7:
            print(f'\nFAIL [test usertask]: expected 7 killed tasks, saw {kills}')
            failures += 1

        for bad in ('Failed to move task to user space', 'NOT KILLED', 'FAILED'):
```
> - `'FAILED'` matches only lines from `hello.c` (`MALLOC FAILED`, `demand paging FAILED`,
>   `deep stack FAILED`); nothing else in the kernel prints it today. The kernel's own failure
>   lines use `FAIL:`.
> - The kill count relies on the `LogWarning` from `HandleUserTaskFault` reaching the console
>   inside the captured `test usertask` output. The default log level is `INFO`, so warnings
>   are printed. Confirm it on the first run by printing `kills`. If log output turns out to
>   be asynchronous and lands after the prompt, drop this check rather than loosening it.
>
> Also add the new `test addrspace` lines to that command's check, so a regression in the
> kernel-side unit check fails the suite even if the overall PASS line is somehow printed:
> ```python
>         output = send_command('test addrspace')
>         check('test addrspace', output, 'PASS: address space test', 'page accounting:')
> ```
> (`check(name, output, *required)` already takes several required substrings; it is called
> that way for `test usertask`.)

---

## 9. Verification

Run from `rpibaremetalos/`:

1. **Host unit tests** (fast, no QEMU): `make test`
2. **Full aarch64 image build:** `make` (the `all` target). It must produce a linking `kernel8.img`.
3. **QEMU regression:** `make qemu-regression` (rpi3 + rpi4). `test/tools/qemu_regression_test.py`
   runs two passes per machine (default alignment and `strict_align=1`, after Phase 0), and both
   must pass. This is the gate that proves:
   - heap demand paging works and hands out zeroed frames (case 7);
   - stack demand paging works: every EL0 case's first push, plus case 10 at depth;
   - syscall copies fault pages in (case 11);
   - wild pointers above the break and into the guard gap still kill (cases 8, 9);
   - cases 1/1b/2/3/5 still kill exactly as before.

4. **Real hardware:** boot the image on a Pi 3, Pi 4 and Pi 5 and run the manual spot-checks
   below. QEMU doesn't model the Pi 5.

Manual CLI spot-checks on a booted image (`make qemu-rpi4`, `make qemu-rpi3`, and real boards):
- `test addrspace` → the eight `fault-in …` lines as expected, `page accounting: 7 pages used,
  all returned`, then `PASS: address space test`
- `test usertask` → `case 7 … OK`, `case 10 … OK`, `case 11 … OK`, seven `task killed`
  warnings, no `NOT KILLED`
- Run `test usertask` and then `test addrspace` several times in a row; every run must pass.
  The `test addrspace` page-accounting check is the leak detector for the teardown walk.

### 9.1 Prove the tests catch the bugs (one-off, do not commit)

A test that can't fail proves nothing. After everything passes, temporarily break each piece
below, rebuild, and confirm the named check fails. Then revert.

| Temporary break | Must fail |
|---|---|
| `ESR_ELx_EC_DABT_LOW` → `0x25` in `TryHandleDemandPageFault` | every user case (no stack page-in): `hello from EL0` missing, kill count wrong |
| `ESR_ELx_DFSC_IS_TRANSLATION` → `((dfsc) <= 3)` | same as above |
| `cbz w0` → `cbnz w0` | same as above |
| Remove the `TranslateOrFaultIn` call in `CopyStringFromUserSpaceToKernelSpace` (use plain `Translate`) | case 11 line missing |
| `in_heap` ignores `heap_break` (use `layout.heap_top`) | case 8 prints `NOT KILLED`; `fault-in heap AT the break` row |
| Drop `page.SoftwareDemandPaged = 1` | `test addrspace` page accounting (leaks 2) |
| Skip freeing the L3 table in the destructor walk | `test addrspace` page accounting (leaks 2) |
| Walk only `l1_index_start` (not `..end`) | `test addrspace` page accounting (stack-side L2/L3/leaf leak) |

Zeroing (`memset` in `FaultInUserPage`) can't be proven this way, because a freed frame often
happens to be zero already. Case 7's read-before-write check catches it only when a dirty frame
is reused. Confirm it by code review.

---

## 10. Correctness invariants & gotchas

- **EL0 aborts are the "lower EL" classes.** An EL0 data abort is EC `0x24`
  (`ESR_ELx_EC_DABT_LOW`), not `0x25` (`DABT_CUR`, which is EL1 → EL1). Testing `0x25` would
  turn away every user page fault.
- **Translation-fault DFSC is `0x04–0x07`.** `0x00–0x03` are address-size faults. Use
  `ESR_ELx_DFSC_IS_TRANSLATION`, never `dfsc <= 3`.
- **No TLBI on fault-in.** The entry was invalid, and invalid descriptors are never cached in
  the TLB. `dsb ish` publishes the zeroed frame and the descriptor, and the `eret` is context
  synchronizing.
- **AF must be set at fault-in.** Writing the page with `AF = ACCESSED` avoids an immediate
  access-flag fault on the retried instruction.
- **Frames are zeroed.** `GetFreeBlock` doesn't clear memory; `FaultInUserPage` must, or one
  task reads another's old stack.
- **Heap fault-in stops at the break.** Only `[heap_base, heap_break)` pages in. Anything above
  the break is a wild pointer and kills the task, the same as the guard gap.
- **Guard gap is unmappable by construction.** `MapPages` refuses any range overlapping
  `[guard_bottom, stack_bottom)`, and `FaultInUserPage` only accepts live-heap and stack VAs.
- **Two first-touch paths, one implementation.** The EL0 fault handler and `user_access.cpp`
  both go through `TaskImpl::FaultInUserPage`, which is the only place that knows the break.
- **Ownership.** `owned_` = L1 + `MapPages` blocks. Tables and `SoftwareDemandPaged` leaves are
  freed by the teardown walk. A leaf is freed by the walk **or** via its block, never both.
- **Walk only the window slots, and only when `l1_` is non-null.** `Initialize()` zeroes the
  whole L1 itself, so everything under a non-zero window slot is this space's own. Every other
  slot stays zero, so walking them would only cost time. A failed `Initialize()` leaves `l1_`
  null, which is what makes it safe to destroy.
- **Retry semantics come from `KernelExit EL0`.** It restores every GPR, and ELR_EL1 from the
  frame slot `[sp, #16*16]` that `KernelEntry` filled with the faulting PC. So `eret`
  re-executes the faulting instruction against the newly mapped page. Don't "step" that slot
  (that is the EL1 MMIO-probe trick, not applicable here).
- **The handler runs with IRQs masked** (exception entry masks them, and nothing on the EL0
  sync path unmasks them). That is the same context `sc_Malloc` already allocates from, so
  `AllocateUserFrame` is safe here. Keep the handler short: no logging on the success path.
- **Asm ABI.** Test `w0`, not `x0`, for an `int` return. Keep ESR/ELR/FAR in x19–x21 across
  the C call so the kill path logs real values.
- **OOM at fault = kill.** `FaultInUserPage` returns false when `AllocateUserFrame` returns 0
  (from EL0, the asm falls through to `HandleUserTaskFault`; from a syscall, the copy fails).
- **Kernel memory is never demand-paged** (§2.1).
- **`user.ld` is unchanged.** The image still links at `USER_SPACE_BASE + 4MB`. Only the
  heap/guard/stack/window-top moved, and those are runtime layout, not link addresses.

## 11. Files touched (checklist)

Phase 0 — one memory model, no model abstraction (separate commit)
- [ ] `include/platform/mmu_manager.h` — delete `MemoryModelTypes`, its strings, `MemoryModel()`, `ToString`
- [ ] `src/c/platform/mmu_manager.cpp` — no `memory_model=` parsing, no `MemoryModel::Initialize`, board ctors take no argument
- [ ] `include/platform/memory_model.h` — rewrite as a static-only `MemoryModel` class
- [ ] `src/c/platform/memory_model.cpp` — **DELETE**
- [ ] `include/platform/platform_mmu.h` — default ctor, no `memory_model_`, `PublishBootPageTableBases`
- [ ] `rpi3/4/5_memory_manager.h` + `.cpp` — no `MemoryModelTypes` ctor parameter
- [ ] `src/c/platform/address_space.cpp` — `Initialize()` zeroes the L1 (no `SeedTaskL1`, no slot check)
- [ ] Call-site rename (`sed`, step 0.7b): `address_space.cpp`, `user_binary_loader.cpp`,
      `system_calls.cpp`, `task_impl.cpp`, `test_command.cpp`
- [ ] `src/c/main.cpp` — no `Memory Model:` banner; obsolete-setting notice
- [ ] `src/c/cli/test_command.cpp` — test headers don't name a model
- [ ] `src/c/platform/exception_manager.cpp` — physical-address fault hint
- [ ] `Makefile.aarch64.mk` — drop `MM=` / `QEMU_MEMORY_MODEL`, regression comment
- [ ] `test/tools/qemu_regression_test.py` — two passes, no model argument
- [ ] `test/tools/qemu_cli_soak_test.py` — no `memory_model=`
- [ ] Comments: `address_space_layout.h`, `address_space.h`, `address_space.cpp`,
      `test_command.cpp`, `start.S`, `global_variables.S`, `hello.c`, board headers
- [ ] `resources/cmdline.txt` — drop `memory_model=`
- [ ] `AGENTS.md` — enum example

Headers
- [ ] `include/os_memory_config.h` — geometry `#define`s
- [ ] `include/platform/address_space_layout.h` — window/region constants + asserts + comment
- [ ] `include/platform/memory_model.h` — `UserSpaceLayout` struct + `MemoryModel::UserSpace()`
- [ ] `include/platform/platform_mmu.h` — `SoftwareDemandPaged` descriptor bit
- [ ] `include/platform/address_space.h` — `FaultInUserPage` decl, ownership comments
- [ ] `include/sysregs.h` — EC (lower-EL) / DFSC / FnV constants
- [ ] `include/platform/page_fault_handler.h` — **NEW**
- [ ] `include/task/task_impl.h` — `MapIntoUserHeap` → `GrowUserHeap`, add `FaultInUserPage`

Sources
- [ ] `src/c/platform/address_space.cpp` — tables out of `owned_`, multi-slot, guard refusal,
      `MapPages` rollback, `FaultInUserPage`, teardown walk
- [ ] `src/c/platform/page_fault_handler.cpp` — **NEW**
- [ ] `src/asm/isr_process_management.S` — EL0 fault dispatch
- [ ] `src/c/task/task_impl.cpp` — `GrowUserHeap`, `FaultInUserPage`, lazy stack
- [ ] `src/c/task/system_calls.cpp` — `Malloc` calls `GrowUserHeap`
- [ ] `src/c/task/user_access.cpp` — fault in on translate miss

Tests
- [ ] `user/hello.c` — cases 7–11
- [ ] `src/c/cli/test_command.cpp` — register cases 7–11; `FaultInUserPage` + page-accounting checks in `test addrspace`
- [ ] `test/tools/qemu_regression_test.py` — assert cases 7, 10, 11; kill count == 7; `page accounting:`
