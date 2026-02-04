# TLA+ Model for Lock-Free Allocator

This directory contains a TLA+ specification for formally verifying the lock-free single block memory allocator with tagged pointers for ABA protection and safe metadata reclamation.

## Specification Files

### LockfreeAllocatorTagged.tla

Models the current C++ implementation with full fidelity including:

**Core Features:**
- **Block states matching C++ enum exactly** - INVALID, IN_USE, AVAILABLE, LOCKED
- **Metadata states** - INVALID, IN_USE, SOFT_DELETED, METADATA_AVAILABLE
- **Bump allocator** - `nextEmptyBlock` and `nextMetadataIndex` for fresh allocation
- **Tagged pointers** - Version counters for ABA protection on all CAS operations
- **Per-CPU sharding** - Separate free lists per CPU to reduce contention

**Ownership Semantics:**
- **Any CPU can deallocate any block** - Matches C++ where any thread with a valid pointer can deallocate
- **CAS provides mutual exclusion** - `CAS(IN_USE -> LOCKED)` prevents double-free
- **`allocatedBy` is for verification only** - Not a constraint on deallocation

**Metadata Reclamation:**
- **Soft-deleted metadata list** - Per-CPU list of metadata awaiting reclamation
- **Free metadata list** - Per-CPU list of reusable metadata records
- **Iterator tracking** - `activeIteratorCount` and `hardDeleteCutoff` control safe reclamation
- **Reclamation pass** - Moves old soft-deleted metadata to free list when safe

**Config files:**
- `LockfreeAllocatorTagged.cfg` - Default configuration (2 CPUs, 3 blocks)
- `LockfreeAllocatorTagged_small.cfg` - Minimal config for quick checks
- `LockfreeAllocatorTagged_core.cfg` - Core safety only (no TypeOK bounds)
- `LockfreeAllocatorTagged_bounded.cfg` - Bounded model checking

## What This Model Verifies

1. **No ABA Problem** - Tagged pointers prevent corrupted CAS operations
2. **No Double-Free** - CAS(IN_USE -> LOCKED) ensures only one CPU can deallocate
3. **No Concurrent Dealloc** - LOCKED state serializes deallocation attempts
4. **Block Conservation** - INVALID + IN_USE + LOCKED + AVAILABLE = NumBlocks
5. **Metadata Conservation** - INVALID + IN_USE + SOFT_DELETED + METADATA_AVAILABLE = NumBlocks
6. **Bump Allocator Consistency** - Blocks at/after bump pointer are INVALID
7. **Interrupt Protection** - All CAS loops protected by interrupt guards

## Understanding the Model

### Block States (matches C++ `allocation_state` enum)

| State | C++ Value | Description |
|-------|-----------|-------------|
| `INVALID` | 0 | Never allocated (virgin memory) |
| `IN_USE` | 1 | Currently allocated and owned |
| `AVAILABLE` | 2 | On free list, ready for reallocation |
| `LOCKED` | 5 | Being deallocated (transient) |

### Metadata States

| State | C++ Value | Description |
|-------|-----------|-------------|
| `INVALID` | 0 | Never allocated |
| `IN_USE` | 1 | Associated with an allocated block |
| `SOFT_DELETED` | 3 | Awaiting safe reclamation (iterators may reference) |
| `METADATA_AVAILABLE` | 4 | On free metadata list, ready for reuse |

### State Variables

**Memory Management:**
- `blockState` - State of each memory block
- `metadataState` - State of each metadata record
- `nextEmptyBlock` - Bump pointer for block allocation
- `nextMetadataIndex` - Bump pointer for metadata allocation
- `blockMetadataIndex` - Maps blocks to their metadata records

**Free Lists (per-CPU):**
- `freeListHead`, `freeListVersion`, `nextFreeBlock` - Block free list
- `softDeletedHead`, `softDeletedVersion`, `nextSoftDeleted` - Soft-deleted metadata
- `freeMetadataHead`, `freeMetadataVersion`, `nextFreeMetadata` - Free metadata

**Iterator/Reclamation:**
- `activeIteratorCount` - Number of active iterators
- `hardDeleteCutoff` - Monotonic counter threshold for safe reclamation
- `monotonicCounter` - Global time counter
- `softDeletedAt` - When each metadata was soft-deleted
- `reclaimInProgress` - Serializes reclamation passes

### Key Invariants

- `NoDoubleFree` - IN_USE blocks have an owner
- `LockedBlocksOwnedByDealloc` - LOCKED blocks are being deallocated by exactly one CPU
- `NoConcurrentDealloc` - Only one CPU can deallocate a block at a time
- `BlockConservation` - Total blocks constant
- `MetadataConservation` - Total metadata records constant
- `BumpAllocatorConsistency` - Unallocated blocks are INVALID
- `AllocCASProtected` - Allocation CAS protected by interrupt guard
- `DeallocPushProtected` - Deallocation push protected by interrupt guard

### Allocation Flow

```
StartAllocation (enter interrupt guard, read free list head)
    |
    +-> [free list not empty] ReadAllocNext -> CASAllocPop
    |       -> CompleteAllocFromFreeList (move old metadata to soft-deleted/free)
    |
    +-> [free list empty] StartBumpAllocation -> CompleteBumpAllocation
```

### Deallocation Flow

```
StartDeallocation (CAS IN_USE -> LOCKED)
    |
    +-> [is last block] CheckLastBlock -> CompleteReclaimLastBlock
    |       -> CompleteSoftDeleteMetadata (metadata to soft-deleted/free list)
    |
    +-> [not last block] StartDeallocPush -> CompleteDeallocPush
            (block to free list, metadata stays with block)
```

### Metadata Reclamation Flow

```
CreateIterator
    -> Sets hardDeleteCutoff to current counter (blocks reclamation)

DestroyIterator
    -> If last iterator, clears hardDeleteCutoff
    -> Triggers reclamation if soft-deleted list not empty

StartReclamation (if no iterators and soft-deleted list not empty)
    -> ReclaimMetadata (for each soft-deleted entry)
    -> EndReclamation
```

## Relationship to C++ Implementation

### Key Correspondences

| TLA+ | C++ | Notes |
|------|-----|-------|
| `blockState` | `allocation_state` enum | INVALID, IN_USE, AVAILABLE, LOCKED |
| `metadataState` | `state_` in `block_metadata` | Tracks metadata lifecycle |
| `nextEmptyBlock` | `next_empty_memory_block_` | Bump allocator for blocks |
| `nextMetadataIndex` | `current_metadata_record_count_` | Bump allocator for metadata |
| `freeListHead[cpu]` | `free_block_bins_[bin * shards + shard]` | Per-CPU free lists |
| `softDeletedHead[cpu]` | `soft_deleted_metadata_heads_[shard]` | Soft-deleted metadata |
| `freeMetadataHead[cpu]` | `free_metadata_heads_[shard]` | Reusable metadata |
| `activeIteratorCount` | `number_of_active_iterators_` | Iterator tracking |
| `hardDeleteCutoff` | `hard_delete_before_counter_cutoff_` | Safe reclamation threshold |
| `monotonicCounter` | `platform::get_monotonic_counter()` | Time ordering |

### Ownership Semantics

The TLA+ model accurately reflects that **any CPU can deallocate any block**:

```cpp
// C++ - no ownership check, just CAS for mutual exclusion
uint8_t current_state = IN_USE;
if (!block_to_deallocate->state_.compare_exchange_strong(current_state, LOCKED, ...))
    return;  // Someone else got it first
```

```tla
\* TLA+ - no allocatedBy constraint, CAS modeled as precondition
StartDeallocation(cpu, block) ==
    ...
    /\ blockState[block] = "IN_USE"  \* Only check state, not owner
    /\ blockState' = [blockState EXCEPT ![block] = "LOCKED"]  \* Atomic CAS
```

The `allocatedBy` variable in TLA+ is **for verification only** - it tracks ownership for safety property checking but does not constrain which CPU can perform deallocation.

### What's Abstracted Away

- **Size bins** - 257 size classes in C++, single list per CPU in TLA+
- **Hash verification** - `hash_check` extension for corruption detection
- **Detailed counter arithmetic** - Simplified monotonic counter

## Running the Model Checker

```bash
cd /home/steve/dev/RPIBareMetalOS/minimalstdlib/tlaplus/lockfree_allocator

# Quick check
./run_tlc.sh LockfreeAllocatorTagged_small.cfg

# Full verification
./run_tlc.sh LockfreeAllocatorTagged.cfg
```

## Expected Results

Model checking should complete with no errors:
```
Model checking completed. No error has been found.
  44845406 states generated, 9073320 distinct states found
```

Note: The full model with metadata reclamation has a large state space. The `TightBound` constraint limits version counters to 10 and monotonic counter to 10 for tractable verification. Without bounds, the state space grows very quickly (100M+ states).
