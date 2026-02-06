---------------------------- MODULE LockfreeAllocatorTagged ----------------------------
(***************************************************************************)
(* TLA+ specification of lock-free allocator with TAGGED POINTERS,         *)
(* PER-CPU SHARDED FREE LISTS, BUMP ALLOCATOR, and METADATA RECLAMATION.   *)
(*                                                                         *)
(* Block states match C++ enum allocation_state exactly:                   *)
(*   INVALID (0), IN_USE (1), AVAILABLE (2), SOFT_DELETED (3),             *)
(*   METADATA_AVAILABLE (4), LOCKED (5)                                    *)
(*                                                                         *)
(* PACKED BLOCK STATE (C++ block_state_):                                  *)
(* The C++ implementation packs memory_block pointer, state, and version   *)
(* into a single atomic<uint64_t>:                                         *)
(*   Layout: [ptr:48][state:8][version:8] = 64 bits                        *)
(*                                                                         *)
(* This allows the deallocation CAS to atomically verify BOTH the pointer  *)
(* AND state, providing stronger ABA protection. The version counter       *)
(* increments on every state transition, preventing the ABA problem where  *)
(* a block cycles through states and ends up back at IN_USE.               *)
(*                                                                         *)
(* Key features modeled:                                                   *)
(* - Packed block_state_ with version counter for ABA protection           *)
(* - Bump allocator for fresh block/metadata allocation                    *)
(* - Per-CPU sharded free lists with tagged-pointer CAS for ABA protection *)
(* - is_last_block optimization for reclaiming the last allocated block    *)
(* - LOCKED state for atomic deallocation (CAS IN_USE -> LOCKED)           *)
(* - Soft-delete mechanism for safe metadata reclamation with iterators    *)
(* - Iterator lifecycle affecting metadata reclamation timing              *)
(* - Any CPU can deallocate any block (CAS provides mutual exclusion)      *)
(* - Protection against reclaiming metadata for in-progress deallocations  *)
(*   (models C++ soft_deleted_at_counter_ = SIZE_MAX guard)                *)
(*                                                                         *)
(* The ownership tracking (allocatedBy) is for verification only - the     *)
(* actual C++ code allows any thread with a valid pointer to deallocate.   *)
(***************************************************************************)

EXTENDS Integers, FiniteSets

CONSTANTS
    NumBlocks,          \* Number of memory blocks to model
    NumCPUs,            \* Number of CPUs (cores)
    MaxInterruptDepth   \* Maximum interrupt nesting depth

VARIABLES
    \* ===== BLOCK MEMORY STATE (models C++ block_state_) =====
    \* The C++ code packs pointer + state + version into atomic<uint64_t>:
    \*   block_state_ = [ptr:48][state:8][version:8]
    \* We model this as separate variables but the CAS semantics check both
    \* state AND version atomically, matching the packed CAS behavior.
    \* Note: SOFT_DELETED and METADATA_AVAILABLE apply to metadata records
    \* that have been detached from their block (after is_last_block reclaim
    \* or when reusing block from free list with new metadata)
    blockState,         \* "INVALID", "IN_USE", "LOCKED", "AVAILABLE"
    blockStateVersion,  \* Version counter for ABA protection (part of packed block_state_)

    \* ===== METADATA STATE =====
    \* Tracks metadata records separately from block memory
    \* Metadata can be: linked to a block (IN_USE/AVAILABLE), or
    \* in soft-deleted list (SOFT_DELETED), or in free list (METADATA_AVAILABLE)
    metadataState,      \* "INVALID", "IN_USE", "SOFT_DELETED", "METADATA_AVAILABLE"

    \* ===== BUMP ALLOCATOR =====
    nextEmptyBlock,     \* Next block index for fresh allocation
    nextMetadataIndex,  \* Next metadata index for fresh allocation

    \* ===== PER-CPU FREE BLOCK LISTS =====
    freeListHead,       \* Per-CPU head block index (0 = empty)
    freeListVersion,    \* Per-CPU version counter for ABA protection
    nextFreeBlock,      \* Next pointers for free list linkage
    blockMetadataIndex, \* Which metadata record is associated with each block

    \* ===== PER-CPU SOFT-DELETED METADATA LISTS =====
    softDeletedHead,    \* Per-CPU head of soft-deleted metadata (0 = empty)
    softDeletedVersion, \* Per-CPU version counter for ABA protection
    nextSoftDeleted,    \* Next pointers for soft-deleted list

    \* ===== PER-CPU FREE METADATA LISTS =====
    freeMetadataHead,   \* Per-CPU head of free metadata (0 = empty)
    freeMetadataVersion,\* Per-CPU version counter for ABA protection
    nextFreeMetadata,   \* Next pointers for free metadata list

    \* ===== ITERATOR/RECLAMATION STATE =====
    \* Models hard_delete_before_counter_cutoff_ and number_of_active_iterators_
    activeIteratorCount,\* Number of active iterators (global)
    hardDeleteCutoff,   \* Counter value for safe reclamation (0 = no iterators)
    monotonicCounter,   \* Global monotonic counter (simplified)
    softDeletedAt,      \* When each metadata was soft-deleted
    reclaimInProgress,  \* Is a reclamation pass in progress?

    \* ===== PER-CPU ALLOCATION STATE =====
    cpuAllocState,      \* "IDLE", "ALLOC_READING", "ALLOC_GOT_NEXT", "ALLOC_BUMP", "ALLOC_GET_META"
    cpuAllocHead,       \* Snapshot of head index for CAS comparison
    cpuAllocVersion,    \* Snapshot of version for CAS comparison
    cpuAllocNext,       \* Snapshot of next pointer
    cpuAllocBlock,      \* Block being allocated (for free list path)

    \* ===== PER-CPU DEALLOCATION STATE =====
    cpuDeallocState,    \* "IDLE", "DEALLOC_CHECKING", "DEALLOC_LOCKED", "DEALLOC_PUSHING", "DEALLOC_RECLAIM"
    cpuDeallocBlock,    \* Block being deallocated
    cpuDeallocBlockVersion, \* Snapshot of block's version for CAS (part of packed block_state_)
    cpuDeallocHead,     \* Snapshot of head for push CAS
    cpuDeallocVersion,  \* Snapshot of version for push CAS

    \* ===== INTERRUPT STATE =====
    cpuInterruptDepth,
    cpuInterruptsEnabled,

    \* ===== OWNERSHIP TRACKING (for verification only) =====
    \* NOTE: This is NOT a constraint on deallocation - any CPU can deallocate
    \* any IN_USE block. The CAS(IN_USE->LOCKED) provides mutual exclusion.
    allocatedBy         \* Which CPU owns each block (0 = unowned)

vars == <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
          freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
          softDeletedHead, softDeletedVersion, nextSoftDeleted,
          freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
          activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
          cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
          cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
          cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

MaxVersion == 64
MaxCounter == 100  \* Bound for monotonic counter in model checking

IncVersion(v) == v + 1

TypeOK ==
    /\ blockState \in [1..NumBlocks -> {"INVALID", "IN_USE", "LOCKED", "AVAILABLE"}]
    /\ blockStateVersion \in [1..NumBlocks -> 0..MaxVersion]
    /\ metadataState \in [1..NumBlocks -> {"INVALID", "IN_USE", "SOFT_DELETED", "METADATA_AVAILABLE"}]
    /\ nextEmptyBlock \in 1..(NumBlocks + 1)
    /\ nextMetadataIndex \in 1..(NumBlocks + 1)
    /\ freeListHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ freeListVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ nextFreeBlock \in [1..NumBlocks -> 0..NumBlocks]
    /\ blockMetadataIndex \in [1..NumBlocks -> 0..NumBlocks]
    /\ softDeletedHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ softDeletedVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ nextSoftDeleted \in [1..NumBlocks -> 0..NumBlocks]
    /\ freeMetadataHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ freeMetadataVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ nextFreeMetadata \in [1..NumBlocks -> 0..NumBlocks]
    /\ activeIteratorCount \in 0..NumCPUs
    /\ hardDeleteCutoff \in 0..MaxCounter
    /\ monotonicCounter \in 0..MaxCounter
    /\ softDeletedAt \in [1..NumBlocks -> 0..MaxCounter]
    /\ reclaimInProgress \in BOOLEAN
    /\ cpuAllocState \in [1..NumCPUs -> {"IDLE", "ALLOC_READING", "ALLOC_GOT_NEXT", "ALLOC_BUMP", "ALLOC_GET_META"}]
    /\ cpuAllocHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuAllocVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ cpuAllocNext \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuAllocBlock \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuDeallocState \in [1..NumCPUs -> {"IDLE", "DEALLOC_CHECKING", "DEALLOC_LOCKED", "DEALLOC_PUSHING", "DEALLOC_RECLAIM"}]
    /\ cpuDeallocBlock \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuDeallocBlockVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ cpuDeallocHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuDeallocVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ cpuInterruptDepth \in [1..NumCPUs -> 0..MaxInterruptDepth]
    /\ cpuInterruptsEnabled \in [1..NumCPUs -> BOOLEAN]
    /\ allocatedBy \in [1..NumBlocks -> 0..NumCPUs]

-----------------------------------------------------------------------------
(* Initialization *)

Init ==
    /\ blockState = [b \in 1..NumBlocks |-> "INVALID"]
    /\ blockStateVersion = [b \in 1..NumBlocks |-> 0]
    /\ metadataState = [m \in 1..NumBlocks |-> "INVALID"]
    /\ nextEmptyBlock = 1
    /\ nextMetadataIndex = 1
    /\ freeListHead = [c \in 1..NumCPUs |-> 0]
    /\ freeListVersion = [c \in 1..NumCPUs |-> 0]
    /\ nextFreeBlock = [b \in 1..NumBlocks |-> 0]
    /\ blockMetadataIndex = [b \in 1..NumBlocks |-> 0]
    /\ softDeletedHead = [c \in 1..NumCPUs |-> 0]
    /\ softDeletedVersion = [c \in 1..NumCPUs |-> 0]
    /\ nextSoftDeleted = [m \in 1..NumBlocks |-> 0]
    /\ freeMetadataHead = [c \in 1..NumCPUs |-> 0]
    /\ freeMetadataVersion = [c \in 1..NumCPUs |-> 0]
    /\ nextFreeMetadata = [m \in 1..NumBlocks |-> 0]
    /\ activeIteratorCount = 0
    /\ hardDeleteCutoff = 0
    /\ monotonicCounter = 1
    /\ softDeletedAt = [m \in 1..NumBlocks |-> 0]
    /\ reclaimInProgress = FALSE
    /\ cpuAllocState = [c \in 1..NumCPUs |-> "IDLE"]
    /\ cpuAllocHead = [c \in 1..NumCPUs |-> 0]
    /\ cpuAllocVersion = [c \in 1..NumCPUs |-> 0]
    /\ cpuAllocNext = [c \in 1..NumCPUs |-> 0]
    /\ cpuAllocBlock = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocState = [c \in 1..NumCPUs |-> "IDLE"]
    /\ cpuDeallocBlock = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocBlockVersion = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocHead = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocVersion = [c \in 1..NumCPUs |-> 0]
    /\ cpuInterruptDepth = [c \in 1..NumCPUs |-> 0]
    /\ cpuInterruptsEnabled = [c \in 1..NumCPUs |-> TRUE]
    /\ allocatedBy = [b \in 1..NumBlocks |-> 0]

-----------------------------------------------------------------------------
(* ALLOCATION - Two paths: free list (preferred) or bump allocator         *)

\* Step 1: Start allocation - enter interrupt guard, read free list head
StartAllocation(cpu) ==
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuDeallocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_READING"]
    /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
    /\ cpuAllocHead' = [cpuAllocHead EXCEPT ![cpu] = freeListHead[cpu]]
    /\ cpuAllocVersion' = [cpuAllocVersion EXCEPT ![cpu] = freeListVersion[cpu]]
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, allocatedBy>>

\* Step 2a: Free list not empty - read next pointer
ReadAllocNext(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_READING"
    /\ cpuAllocHead[cpu] # 0
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ cpuAllocNext' = [cpuAllocNext EXCEPT ![cpu] = nextFreeBlock[cpuAllocHead[cpu]]]
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_GOT_NEXT"]
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocHead, cpuAllocVersion, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Step 2b: Free list empty - switch to bump allocation
StartBumpAllocation(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_READING"
    /\ cpuAllocHead[cpu] = 0
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_BUMP"]
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Step 3a: CAS to pop from free list
\* On success: block goes to IN_USE (incrementing version), old metadata goes to soft-deleted list
CASAllocPop(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_GOT_NEXT"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ LET head == cpuAllocHead[cpu]
           version == cpuAllocVersion[cpu]
           next == cpuAllocNext[cpu]
       IN
       IF freeListHead[cpu] = head /\ freeListVersion[cpu] = version
       THEN \* CAS succeeds - we now own the block
          /\ freeListHead' = [freeListHead EXCEPT ![cpu] = next]
          /\ freeListVersion' = [freeListVersion EXCEPT ![cpu] = IncVersion(version)]
          /\ blockState' = [blockState EXCEPT ![head] = "IN_USE"]
          /\ blockStateVersion' = [blockStateVersion EXCEPT ![head] = IncVersion(blockStateVersion[head])]
          /\ allocatedBy' = [allocatedBy EXCEPT ![head] = cpu]  \* Mark ownership immediately
          /\ cpuAllocBlock' = [cpuAllocBlock EXCEPT ![cpu] = head]
          /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_GET_META"]
          \* Old metadata will be moved to soft-deleted in next step
          /\ UNCHANGED <<metadataState, nextEmptyBlock, nextMetadataIndex,
                         nextFreeBlock, blockMetadataIndex,
                         softDeletedHead, softDeletedVersion, nextSoftDeleted,
                         freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                         activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                         cpuAllocHead, cpuAllocVersion, cpuAllocNext,
                         cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                         cpuInterruptDepth, cpuInterruptsEnabled>>
       ELSE \* CAS fails - retry
          /\ cpuAllocHead' = [cpuAllocHead EXCEPT ![cpu] = freeListHead[cpu]]
          /\ cpuAllocVersion' = [cpuAllocVersion EXCEPT ![cpu] = freeListVersion[cpu]]
          /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_READING"]
          /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                         freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                         softDeletedHead, softDeletedVersion, nextSoftDeleted,
                         freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                         activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                         cpuAllocNext, cpuAllocBlock,
                         cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                         cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Step 3b: Complete bump allocation
\* Sets block state to IN_USE and increments version (part of packed block_state_)
CompleteBumpAllocation(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_BUMP"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ nextEmptyBlock <= NumBlocks
    /\ nextMetadataIndex <= NumBlocks
    /\ LET block == nextEmptyBlock
           meta == nextMetadataIndex
       IN
       /\ nextEmptyBlock' = nextEmptyBlock + 1
       /\ nextMetadataIndex' = nextMetadataIndex + 1
       /\ blockState' = [blockState EXCEPT ![block] = "IN_USE"]
       /\ blockStateVersion' = [blockStateVersion EXCEPT ![block] = IncVersion(blockStateVersion[block])]
       /\ metadataState' = [metadataState EXCEPT ![meta] = "IN_USE"]
       /\ blockMetadataIndex' = [blockMetadataIndex EXCEPT ![block] = meta]
       /\ allocatedBy' = [allocatedBy EXCEPT ![block] = cpu]
       /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "IDLE"]
       /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
       /\ UNCHANGED <<freeListHead, freeListVersion, nextFreeBlock,
                      softDeletedHead, softDeletedVersion, nextSoftDeleted,
                      freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                      activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                      cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                      cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                      cpuInterruptDepth>>

\* Bump allocation failed - out of memory
BumpAllocationFailed(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_BUMP"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ (nextEmptyBlock > NumBlocks \/ nextMetadataIndex > NumBlocks)
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "IDLE"]
    /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, allocatedBy>>

\* Complete allocation from free list - get metadata (from free list or bump)
\* Also move old metadata to soft-deleted list
CompleteAllocFromFreeList(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_GET_META"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ LET block == cpuAllocBlock[cpu]
           oldMeta == blockMetadataIndex[block]
           \* Try to get metadata from free list, else bump allocate
           newMeta == IF freeMetadataHead[cpu] # 0
                      THEN freeMetadataHead[cpu]
                      ELSE nextMetadataIndex
       IN
       /\ newMeta <= NumBlocks
       \* Update metadata tracking
       /\ IF freeMetadataHead[cpu] # 0
          THEN \* Pop from free metadata list
             /\ freeMetadataHead' = [freeMetadataHead EXCEPT ![cpu] = nextFreeMetadata[freeMetadataHead[cpu]]]
             /\ freeMetadataVersion' = [freeMetadataVersion EXCEPT ![cpu] = IncVersion(freeMetadataVersion[cpu])]
             /\ nextMetadataIndex' = nextMetadataIndex
          ELSE \* Bump allocate metadata
             /\ nextMetadataIndex' = nextMetadataIndex + 1
             /\ freeMetadataHead' = freeMetadataHead
             /\ freeMetadataVersion' = freeMetadataVersion
       /\ metadataState' = [metadataState EXCEPT
                            ![newMeta] = "IN_USE",
                            ![oldMeta] = IF hardDeleteCutoff = 0
                                         THEN "METADATA_AVAILABLE"
                                         ELSE "SOFT_DELETED"]
       /\ blockMetadataIndex' = [blockMetadataIndex EXCEPT ![block] = newMeta]
       \* Push old metadata to appropriate list
       /\ IF hardDeleteCutoff = 0
          THEN \* No iterators - go directly to free metadata list
             /\ nextFreeMetadata' = [nextFreeMetadata EXCEPT ![oldMeta] = freeMetadataHead[cpu]]
             /\ softDeletedHead' = softDeletedHead
             /\ softDeletedVersion' = softDeletedVersion
             /\ nextSoftDeleted' = nextSoftDeleted
             /\ softDeletedAt' = softDeletedAt
          ELSE \* Has iterators - go to soft-deleted list
             /\ nextSoftDeleted' = [nextSoftDeleted EXCEPT ![oldMeta] = softDeletedHead[cpu]]
             /\ softDeletedHead' = [softDeletedHead EXCEPT ![cpu] = oldMeta]
             /\ softDeletedVersion' = [softDeletedVersion EXCEPT ![cpu] = IncVersion(softDeletedVersion[cpu])]
             /\ softDeletedAt' = [softDeletedAt EXCEPT ![oldMeta] = monotonicCounter]
             /\ nextFreeMetadata' = nextFreeMetadata
       /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "IDLE"]
       /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
       \* Note: allocatedBy was already set in CASAllocPop
       /\ UNCHANGED <<blockState, blockStateVersion, nextEmptyBlock, freeListHead, freeListVersion, nextFreeBlock,
                      activeIteratorCount, hardDeleteCutoff, monotonicCounter, reclaimInProgress,
                      cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                      cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                      cpuInterruptDepth, allocatedBy>>

-----------------------------------------------------------------------------
(* DEALLOCATION - Any CPU can deallocate any IN_USE block                  *)
(* The CAS(IN_USE -> LOCKED) provides mutual exclusion, not ownership.     *)

\* Step 1: Lock block via packed CAS on block_state_ (ptr+state+version)
\* NOTE: No ownership constraint - any CPU can attempt this on any IN_USE block
\* The packed CAS atomically checks both state AND version, providing ABA protection
StartDeallocation(cpu, block) ==
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuDeallocState[cpu] = "IDLE"
    /\ cpuInterruptDepth[cpu] = 0
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ blockState[block] = "IN_USE"
    \* Packed CAS(IN_USE -> LOCKED) with version increment
    \* In C++: CAS checks packed [ptr:48][state:8][version:8] atomically
    /\ blockState' = [blockState EXCEPT ![block] = "LOCKED"]
    /\ blockStateVersion' = [blockStateVersion EXCEPT ![block] = IncVersion(blockStateVersion[block])]
    /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "DEALLOC_LOCKED"]
    /\ cpuDeallocBlock' = [cpuDeallocBlock EXCEPT ![cpu] = block]
    /\ cpuDeallocBlockVersion' = [cpuDeallocBlockVersion EXCEPT ![cpu] = blockStateVersion[block]]
    /\ UNCHANGED <<metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Step 2a: Check if last block - try to reclaim via bump pointer
CheckLastBlock(cpu) ==
    /\ cpuDeallocState[cpu] = "DEALLOC_LOCKED"
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ LET block == cpuDeallocBlock[cpu] IN
       /\ block = nextEmptyBlock - 1
       /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "DEALLOC_RECLAIM"]
       /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
       /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                      freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                      softDeletedHead, softDeletedVersion, nextSoftDeleted,
                      freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                      activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                      cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                      cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                      cpuInterruptDepth, allocatedBy>>

\* Step 2b: Not last block - proceed to push to free list
\* Changes state from LOCKED to AVAILABLE, incrementing version (part of packed block_state_)
StartDeallocPush(cpu) ==
    /\ cpuDeallocState[cpu] = "DEALLOC_LOCKED"
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ LET block == cpuDeallocBlock[cpu] IN
       /\ block # nextEmptyBlock - 1
       /\ blockState[block] = "LOCKED"
       /\ blockState' = [blockState EXCEPT ![block] = "AVAILABLE"]
       /\ blockStateVersion' = [blockStateVersion EXCEPT ![block] = IncVersion(blockStateVersion[block])]
       /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
       /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "DEALLOC_PUSHING"]
       /\ cpuDeallocHead' = [cpuDeallocHead EXCEPT ![cpu] = freeListHead[cpu]]
       /\ cpuDeallocVersion' = [cpuDeallocVersion EXCEPT ![cpu] = freeListVersion[cpu]]
       /\ nextFreeBlock' = [nextFreeBlock EXCEPT ![block] = freeListHead[cpu]]
       /\ UNCHANGED <<metadataState, nextEmptyBlock, nextMetadataIndex,
                      freeListHead, freeListVersion, blockMetadataIndex,
                      softDeletedHead, softDeletedVersion, nextSoftDeleted,
                      freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                      activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                      cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                      cpuDeallocBlock, cpuDeallocBlockVersion, cpuInterruptDepth, allocatedBy>>

\* Step 3a: Complete reclaim of last block AND move metadata atomically
\* This combines the bump pointer CAS and metadata handling to match C++ where
\* move_metadata_to_soft_deleted_list is called immediately after bump CAS succeeds
\* State changes from LOCKED to INVALID, incrementing version (part of packed block_state_)
CompleteReclaimLastBlock(cpu) ==
    /\ cpuDeallocState[cpu] = "DEALLOC_RECLAIM"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ LET block == cpuDeallocBlock[cpu]
           meta == blockMetadataIndex[block]
       IN
       IF block = nextEmptyBlock - 1
       THEN \* CAS succeeds - reclaim block and handle metadata atomically
          /\ nextEmptyBlock' = block
          /\ blockState' = [blockState EXCEPT ![block] = "INVALID"]
          /\ blockStateVersion' = [blockStateVersion EXCEPT ![block] = IncVersion(blockStateVersion[block])]
          /\ allocatedBy' = [allocatedBy EXCEPT ![block] = 0]
          \* Handle metadata: to free list (no iterators) or soft-deleted (has iterators)
          /\ IF hardDeleteCutoff = 0
             THEN \* No iterators - go directly to free metadata list
                /\ metadataState' = [metadataState EXCEPT ![meta] = "METADATA_AVAILABLE"]
                /\ nextFreeMetadata' = [nextFreeMetadata EXCEPT ![meta] = freeMetadataHead[cpu]]
                /\ freeMetadataHead' = [freeMetadataHead EXCEPT ![cpu] = meta]
                /\ freeMetadataVersion' = [freeMetadataVersion EXCEPT ![cpu] = IncVersion(freeMetadataVersion[cpu])]
                /\ softDeletedHead' = softDeletedHead
                /\ softDeletedVersion' = softDeletedVersion
                /\ nextSoftDeleted' = nextSoftDeleted
                /\ softDeletedAt' = softDeletedAt
             ELSE \* Has iterators - go to soft-deleted list
                /\ metadataState' = [metadataState EXCEPT ![meta] = "SOFT_DELETED"]
                /\ nextSoftDeleted' = [nextSoftDeleted EXCEPT ![meta] = softDeletedHead[cpu]]
                /\ softDeletedHead' = [softDeletedHead EXCEPT ![cpu] = meta]
                /\ softDeletedVersion' = [softDeletedVersion EXCEPT ![cpu] = IncVersion(softDeletedVersion[cpu])]
                /\ softDeletedAt' = [softDeletedAt EXCEPT ![meta] = monotonicCounter]
                /\ freeMetadataHead' = freeMetadataHead
                /\ freeMetadataVersion' = freeMetadataVersion
                /\ nextFreeMetadata' = nextFreeMetadata
          /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "IDLE"]
          /\ cpuDeallocBlock' = [cpuDeallocBlock EXCEPT ![cpu] = 0]
          /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
          /\ UNCHANGED <<nextMetadataIndex, freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                         activeIteratorCount, hardDeleteCutoff, monotonicCounter, reclaimInProgress,
                         cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                         cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion, cpuInterruptDepth>>
       ELSE \* CAS failed - fall back to free list
          /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "DEALLOC_LOCKED"]
          /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
          /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                         freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                         softDeletedHead, softDeletedVersion, nextSoftDeleted,
                         freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                         activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                         cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                         cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                         cpuInterruptDepth, allocatedBy>>

\* Step 3b: CAS to push block to free list
CompleteDeallocPush(cpu) ==
    /\ cpuDeallocState[cpu] = "DEALLOC_PUSHING"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ LET block == cpuDeallocBlock[cpu]
           expectedHead == cpuDeallocHead[cpu]
           expectedVersion == cpuDeallocVersion[cpu]
       IN
       IF freeListHead[cpu] = expectedHead /\ freeListVersion[cpu] = expectedVersion
       THEN \* CAS succeeds
          /\ freeListHead' = [freeListHead EXCEPT ![cpu] = block]
          /\ freeListVersion' = [freeListVersion EXCEPT ![cpu] = IncVersion(expectedVersion)]
          /\ allocatedBy' = [allocatedBy EXCEPT ![block] = 0]
          /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "IDLE"]
          /\ cpuDeallocBlock' = [cpuDeallocBlock EXCEPT ![cpu] = 0]
          /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
          /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                         nextFreeBlock, blockMetadataIndex,
                         softDeletedHead, softDeletedVersion, nextSoftDeleted,
                         freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                         activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                         cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                         cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion, cpuInterruptDepth>>
       ELSE \* CAS fails - retry
          /\ cpuDeallocHead' = [cpuDeallocHead EXCEPT ![cpu] = freeListHead[cpu]]
          /\ cpuDeallocVersion' = [cpuDeallocVersion EXCEPT ![cpu] = freeListVersion[cpu]]
          /\ nextFreeBlock' = [nextFreeBlock EXCEPT ![block] = freeListHead[cpu]]
          /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                         freeListHead, freeListVersion, blockMetadataIndex,
                         softDeletedHead, softDeletedVersion, nextSoftDeleted,
                         freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                         activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                         cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                         cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion,
                         cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

-----------------------------------------------------------------------------
(* ITERATOR LIFECYCLE - affects metadata reclamation timing                *)

\* Create an iterator - prevents immediate metadata reclamation
CreateIterator ==
    /\ activeIteratorCount < NumCPUs
    /\ activeIteratorCount' = activeIteratorCount + 1
    /\ IF activeIteratorCount = 0
       THEN hardDeleteCutoff' = monotonicCounter + 1
       ELSE hardDeleteCutoff' = hardDeleteCutoff
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Destroy an iterator - may trigger reclamation
DestroyIterator ==
    /\ activeIteratorCount > 0
    /\ activeIteratorCount' = activeIteratorCount - 1
    /\ IF activeIteratorCount = 1
       THEN hardDeleteCutoff' = 0  \* Clear cutoff when last iterator destroyed
       ELSE hardDeleteCutoff' = hardDeleteCutoff
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Advance monotonic counter (models time passing)
AdvanceCounter ==
    /\ monotonicCounter < MaxCounter
    /\ monotonicCounter' = monotonicCounter + 1
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, softDeletedAt, reclaimInProgress,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

-----------------------------------------------------------------------------
(* METADATA RECLAMATION - move old soft-deleted metadata to free list      *)

\* Start reclamation pass (only one at a time)
StartReclamation ==
    /\ ~reclaimInProgress
    /\ activeIteratorCount = 0  \* Only reclaim when no iterators
    /\ \E cpu \in 1..NumCPUs : softDeletedHead[cpu] # 0
    /\ reclaimInProgress' = TRUE
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Reclaim one metadata record from soft-deleted to free list
\* Guard: Don't reclaim metadata for blocks under active deallocation.
\* This models the C++ soft_deleted_at_counter_ = SIZE_MAX protection that
\* prevents reclamation from interfering with in-progress deallocations.
ReclaimMetadata(cpu, meta) ==
    /\ reclaimInProgress
    /\ softDeletedHead[cpu] = meta
    /\ metadataState[meta] = "SOFT_DELETED"
    \* Protection: metadata must not be associated with a block being deallocated
    /\ \A c \in 1..NumCPUs:
        cpuDeallocState[c] # "IDLE" => blockMetadataIndex[cpuDeallocBlock[c]] # meta
    /\ metadataState' = [metadataState EXCEPT ![meta] = "METADATA_AVAILABLE"]
    /\ softDeletedHead' = [softDeletedHead EXCEPT ![cpu] = nextSoftDeleted[meta]]
    /\ softDeletedVersion' = [softDeletedVersion EXCEPT ![cpu] = IncVersion(softDeletedVersion[cpu])]
    /\ nextFreeMetadata' = [nextFreeMetadata EXCEPT ![meta] = freeMetadataHead[cpu]]
    /\ freeMetadataHead' = [freeMetadataHead EXCEPT ![cpu] = meta]
    /\ freeMetadataVersion' = [freeMetadataVersion EXCEPT ![cpu] = IncVersion(freeMetadataVersion[cpu])]
    /\ nextSoftDeleted' = [nextSoftDeleted EXCEPT ![meta] = 0]
    /\ UNCHANGED <<blockState, blockStateVersion, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* End reclamation pass
EndReclamation ==
    /\ reclaimInProgress
    /\ \A cpu \in 1..NumCPUs : softDeletedHead[cpu] = 0
    /\ reclaimInProgress' = FALSE
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

-----------------------------------------------------------------------------
(* INTERRUPT HANDLING *)

InterruptAllocate(cpu) ==
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ cpuInterruptDepth[cpu] < MaxInterruptDepth
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptDepth' = [cpuInterruptDepth EXCEPT ![cpu] = @ + 1]
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_READING"]
    /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
    /\ cpuAllocHead' = [cpuAllocHead EXCEPT ![cpu] = freeListHead[cpu]]
    /\ cpuAllocVersion' = [cpuAllocVersion EXCEPT ![cpu] = freeListVersion[cpu]]
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   allocatedBy>>

ReturnFromInterrupt(cpu) ==
    /\ cpuInterruptDepth[cpu] > 0
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ cpuInterruptDepth' = [cpuInterruptDepth EXCEPT ![cpu] = @ - 1]
    /\ UNCHANGED <<blockState, blockStateVersion, metadataState, nextEmptyBlock, nextMetadataIndex,
                   freeListHead, freeListVersion, nextFreeBlock, blockMetadataIndex,
                   softDeletedHead, softDeletedVersion, nextSoftDeleted,
                   freeMetadataHead, freeMetadataVersion, nextFreeMetadata,
                   activeIteratorCount, hardDeleteCutoff, monotonicCounter, softDeletedAt, reclaimInProgress,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext, cpuAllocBlock,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocBlockVersion, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptsEnabled, allocatedBy>>

-----------------------------------------------------------------------------
(* Next state relation *)

Next ==
    \/ \E cpu \in 1..NumCPUs :
        \/ StartAllocation(cpu)
        \/ ReadAllocNext(cpu)
        \/ StartBumpAllocation(cpu)
        \/ CASAllocPop(cpu)
        \/ CompleteBumpAllocation(cpu)
        \/ BumpAllocationFailed(cpu)
        \/ CompleteAllocFromFreeList(cpu)
        \/ \E block \in 1..NumBlocks : StartDeallocation(cpu, block)
        \/ CheckLastBlock(cpu)
        \/ StartDeallocPush(cpu)
        \/ CompleteReclaimLastBlock(cpu)
        \/ CompleteDeallocPush(cpu)
        \/ InterruptAllocate(cpu)
        \/ ReturnFromInterrupt(cpu)
        \/ \E meta \in 1..NumBlocks : ReclaimMetadata(cpu, meta)
    \/ CreateIterator
    \/ DestroyIterator
    \/ AdvanceCounter
    \/ StartReclamation
    \/ EndReclamation

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(* Safety Properties *)

\* IN_USE blocks must have an owner (for verification)
NoDoubleFree ==
    \A b \in 1..NumBlocks :
        blockState[b] = "IN_USE" => allocatedBy[b] # 0

\* LOCKED blocks are owned by exactly one deallocating CPU
LockedBlocksOwnedByDealloc ==
    \A b \in 1..NumBlocks :
        blockState[b] = "LOCKED" =>
            \E cpu \in 1..NumCPUs :
                /\ cpuDeallocState[cpu] \in {"DEALLOC_LOCKED", "DEALLOC_PUSHING", "DEALLOC_RECLAIM"}
                /\ cpuDeallocBlock[cpu] = b

\* Only one CPU can deallocate a given block at a time
NoConcurrentDealloc ==
    \A b \in 1..NumBlocks :
    \A cpu1, cpu2 \in 1..NumCPUs :
        (cpuDeallocBlock[cpu1] = b /\ cpuDeallocBlock[cpu2] = b /\
         cpuDeallocState[cpu1] # "IDLE" /\ cpuDeallocState[cpu2] # "IDLE")
        => cpu1 = cpu2

\* Block conservation
BlockConservation ==
    Cardinality({b \in 1..NumBlocks : blockState[b] = "INVALID"}) +
    Cardinality({b \in 1..NumBlocks : blockState[b] = "IN_USE"}) +
    Cardinality({b \in 1..NumBlocks : blockState[b] = "LOCKED"}) +
    Cardinality({b \in 1..NumBlocks : blockState[b] = "AVAILABLE"}) = NumBlocks

\* Metadata conservation
MetadataConservation ==
    Cardinality({m \in 1..NumBlocks : metadataState[m] = "INVALID"}) +
    Cardinality({m \in 1..NumBlocks : metadataState[m] = "IN_USE"}) +
    Cardinality({m \in 1..NumBlocks : metadataState[m] = "SOFT_DELETED"}) +
    Cardinality({m \in 1..NumBlocks : metadataState[m] = "METADATA_AVAILABLE"}) = NumBlocks

\* Bump allocator consistency
BumpAllocatorConsistency ==
    \A b \in 1..NumBlocks :
        b >= nextEmptyBlock => blockState[b] = "INVALID"

\* Reclamation can only START when no iterators exist
\* (but new iterators can be created during reclamation - cutoff mechanism handles safety)
\* This is captured in StartReclamation precondition, not as a global invariant
\* The cutoff-based check in C++ prevents reclaiming entries referenced by new iterators

\* CAS loops are protected by interrupt guard
AllocCASProtected ==
    \A cpu \in 1..NumCPUs :
        cpuAllocState[cpu] \in {"ALLOC_READING", "ALLOC_GOT_NEXT", "ALLOC_BUMP", "ALLOC_GET_META"} =>
            cpuInterruptsEnabled[cpu] = FALSE

DeallocPushProtected ==
    \A cpu \in 1..NumCPUs :
        cpuDeallocState[cpu] \in {"DEALLOC_PUSHING", "DEALLOC_RECLAIM"} =>
            cpuInterruptsEnabled[cpu] = FALSE

CoreSafety ==
    /\ NoDoubleFree
    /\ LockedBlocksOwnedByDealloc
    /\ NoConcurrentDealloc
    /\ BlockConservation
    /\ MetadataConservation
    /\ BumpAllocatorConsistency
    /\ AllocCASProtected
    /\ DeallocPushProtected

Safety == TypeOK /\ CoreSafety

VersionBound ==
    /\ \A c \in 1..NumCPUs:
        /\ freeListVersion[c] <= 20
        /\ softDeletedVersion[c] <= 20
        /\ freeMetadataVersion[c] <= 20
    /\ \A b \in 1..NumBlocks:
        blockStateVersion[b] <= 20

\* Tighter bound for faster verification
TightBound ==
    /\ \A c \in 1..NumCPUs:
        /\ freeListVersion[c] <= 10
        /\ softDeletedVersion[c] <= 10
        /\ freeMetadataVersion[c] <= 10
    /\ \A b \in 1..NumBlocks:
        blockStateVersion[b] <= 10
    /\ monotonicCounter <= 10
    /\ activeIteratorCount <= 2

=============================================================================
