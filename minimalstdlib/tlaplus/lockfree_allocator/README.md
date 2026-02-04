# TLA+ Model for Lock-Free Allocator

This directory contains a TLA+ specification for formally verifying the lock-free single block memory allocator with tagged pointers for ABA protection.

## Specification Files

### LockfreeAllocatorTagged.tla

Models the current implementation with **tagged pointers** (16-bit version counter + 48-bit pointer) for multi-core ABA protection.

**Key features modeled:**
- Version counter incremented on every CAS success
- CAS compares BOTH pointer AND version
- Interrupt guards for same-CPU re-entrancy protection
- Multi-CPU concurrent access

**Config files:**
- `LockfreeAllocatorTagged.cfg` - Default configuration (2 CPUs, 3 blocks)
- `LockfreeAllocatorTagged_small.cfg` - Minimal config for quick checks
- `LockfreeAllocatorTagged_core.cfg` - Core safety only (no TypeOK version bounds)
- `LockfreeAllocatorTagged_bounded.cfg` - Bounded model checking with version constraint

## What This Model Verifies

1. **No ABA Problem** - Tagged pointers prevent corrupted CAS operations across CPUs
2. **Re-entrancy Safety** - Interrupt guards prevent same-CPU ABA
3. **No Double-Free** - A block cannot be freed twice
4. **Block Conservation** - Total blocks remain constant (no leaks or corruption)
5. **Allocation Exclusivity** - Each allocated block has exactly one owner

## Installing TLA+ Tools

### Option 1: TLA+ Toolbox (GUI)
Download from: https://github.com/tlaplus/tlaplus/releases

### Option 2: Command-line TLC
The `tla2tools.jar` is already included. If missing, the `run_tlc.sh` script will download it automatically.

## Running the Model Checker

### Using the Script (recommended)
```bash
cd /home/steve/dev/RPIBareMetalOS/minimalstdlib/tlaplus

# Run with default config
./run_tlc.sh

# Run with specific config
./run_tlc.sh LockfreeAllocatorTagged_small.cfg
./run_tlc.sh LockfreeAllocatorTagged_bounded.cfg
```

### Manual Execution
```bash
cd /home/steve/dev/RPIBareMetalOS/minimalstdlib/tlaplus

# Quick check with small config
java -jar tla2tools.jar -config LockfreeAllocatorTagged_small.cfg LockfreeAllocatorTagged.tla

# Full verification with default config
java -Xmx4g -jar tla2tools.jar -config LockfreeAllocatorTagged.cfg LockfreeAllocatorTagged.tla

# Bounded model checking (limits version counter to bound state space)
java -jar tla2tools.jar -config LockfreeAllocatorTagged_bounded.cfg LockfreeAllocatorTagged.tla
```

Note: State files are written to `/tmp/tlc-lockfree-allocator/` to avoid cluttering the source directory.

## Understanding the Model

### State Variables
- `blockState` - State of each block: FREE, IN_USE, AVAILABLE
- `freeListHead` - Index of first block in free list
- `freeListVersion` - **Version counter** for ABA protection
- `nextFreeBlock` - Linked list next pointers
- `cpuAllocState/cpuDeallocState` - Per-CPU operation state
- `cpuAllocVersion/cpuDeallocVersion` - Per-CPU snapshot of version for CAS
- `cpuInterruptsEnabled` - Whether interrupts are enabled (models interrupt guard)

### Key Invariants
- `NoDoubleFree` - IN_USE blocks have exactly one owner
- `BlockConservation` - Total blocks never changes
- `AllocCASProtected` - Allocation CAS happens with interrupts disabled
- `DeallocPushProtected` - Deallocation push happens with interrupts disabled
- `CoreSafety` - Combined safety properties (without version bounds)
- `Safety` - Full safety including TypeOK

### How Tagged Pointers Prevent ABA

The ABA problem occurs when:
1. CPU1 reads head=A, next=B
2. CPU2 allocates A, then B, then frees A (head returns to A)
3. CPU1's CAS succeeds (head still equals A) but next pointer is now wrong

With version counter:
1. CPU1 reads head=A, version=0, next=B
2. CPU2's operations increment version to 2
3. CPU1's CAS fails because version 0 != 2, forcing a retry with fresh data

## Scaling Up the Model

For more thorough checking, modify the constants in the .cfg file:
```
NumBlocks = 4
NumCPUs = 3
MaxInterruptDepth = 2
```

**Warning**: State space grows exponentially. Larger configs may take minutes to hours.

## Expected Results

Model checking should complete with no errors:
```
Model checking completed. No error has been found.
  States found: ~10,000-100,000
```

## Relationship to C++ Implementation

The TLA+ model verifies the correctness of the algorithm used in:
`include/__memory_resource/lockfree_single_block_resource.h`

Key correspondences:
- `tagged_ptr` struct with 16-bit version = `freeListVersion` variable
- `free_block_bin::compare_exchange_head()` = CAS checking head AND version
- `interrupt_guard` RAII class = `cpuInterruptsEnabled` toggling
- Per-CPU sharding (not modeled) = additional contention reduction in implementation
