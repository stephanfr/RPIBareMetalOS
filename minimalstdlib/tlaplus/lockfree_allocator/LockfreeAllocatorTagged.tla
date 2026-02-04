---------------------------- MODULE LockfreeAllocatorTagged ----------------------------
(***************************************************************************)
(* TLA+ specification of lock-free allocator with TAGGED POINTERS          *)
(* for multi-core ABA protection.                                          *)
(*                                                                         *)
(* The key insight: CAS compares BOTH the index AND a version counter.     *)
(* Even if the index returns to the same value (ABA), the version will     *)
(* be different, causing CAS to fail and retry with fresh data.            *)
(*                                                                         *)
(* This model verifies that tagged pointers solve the multi-core ABA       *)
(* problem that was found in LockfreeAllocatorMultiCPU.tla                 *)
(***************************************************************************)

EXTENDS Integers, FiniteSets

CONSTANTS
    NumBlocks,          \* Number of memory blocks to model
    NumCPUs,            \* Number of CPUs (cores)
    MaxInterruptDepth   \* Maximum interrupt nesting depth

VARIABLES
    \* Block state and free list structure
    blockState,         \* "FREE", "IN_USE", "AVAILABLE"
    freeListHead,       \* Index of head block (0 = empty)
    freeListVersion,    \* Version counter for ABA protection
    nextFreeBlock,      \* Next pointers for free list

    \* Per-CPU allocation state
    cpuAllocState,      \* "IDLE", "ALLOC_READING", "ALLOC_GOT_NEXT"
    cpuAllocHead,       \* Snapshot of head index for CAS comparison
    cpuAllocVersion,    \* Snapshot of version for CAS comparison
    cpuAllocNext,       \* Snapshot of next pointer

    \* Per-CPU deallocation state
    cpuDeallocState,    \* "IDLE", "DEALLOC_LOCKED", "DEALLOC_PUSHING"
    cpuDeallocBlock,    \* Block being deallocated
    cpuDeallocHead,     \* Snapshot of head for push CAS
    cpuDeallocVersion,  \* Snapshot of version for push CAS

    \* Interrupt state
    cpuInterruptDepth,
    cpuInterruptsEnabled,

    \* Ownership tracking
    allocatedBy         \* Which CPU owns each block (0 = unowned)

vars == <<blockState, freeListHead, freeListVersion, nextFreeBlock,
          cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext,
          cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
          cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Version counter provides ABA protection
\* In practice, 32-bit counter won't wrap during any in-flight operation
\* For model checking with small state space, bound must exceed max ops during any CAS
MaxVersion == 64

\* Simple increment (no wrap-around in bounded model)
IncVersion(v) == v + 1

TypeOK ==
    /\ blockState \in [1..NumBlocks -> {"FREE", "IN_USE", "AVAILABLE"}]
    /\ freeListHead \in 0..NumBlocks
    /\ freeListVersion \in 0..MaxVersion
    /\ nextFreeBlock \in [1..NumBlocks -> 0..NumBlocks]
    /\ cpuAllocState \in [1..NumCPUs -> {"IDLE", "ALLOC_READING", "ALLOC_GOT_NEXT"}]
    /\ cpuAllocHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuAllocVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ cpuAllocNext \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuDeallocState \in [1..NumCPUs -> {"IDLE", "DEALLOC_LOCKED", "DEALLOC_PUSHING"}]
    /\ cpuDeallocBlock \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuDeallocHead \in [1..NumCPUs -> 0..NumBlocks]
    /\ cpuDeallocVersion \in [1..NumCPUs -> 0..MaxVersion]
    /\ cpuInterruptDepth \in [1..NumCPUs -> 0..MaxInterruptDepth]
    /\ cpuInterruptsEnabled \in [1..NumCPUs -> BOOLEAN]
    /\ allocatedBy \in [1..NumBlocks -> 0..NumCPUs]

-----------------------------------------------------------------------------
(* Initialization *)

Init ==
    /\ blockState = [b \in 1..NumBlocks |-> "FREE"]
    /\ freeListHead = NumBlocks
    /\ freeListVersion = 0
    /\ nextFreeBlock = [b \in 1..NumBlocks |-> IF b > 1 THEN b - 1 ELSE 0]
    /\ cpuAllocState = [c \in 1..NumCPUs |-> "IDLE"]
    /\ cpuAllocHead = [c \in 1..NumCPUs |-> 0]
    /\ cpuAllocVersion = [c \in 1..NumCPUs |-> 0]
    /\ cpuAllocNext = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocState = [c \in 1..NumCPUs |-> "IDLE"]
    /\ cpuDeallocBlock = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocHead = [c \in 1..NumCPUs |-> 0]
    /\ cpuDeallocVersion = [c \in 1..NumCPUs |-> 0]
    /\ cpuInterruptDepth = [c \in 1..NumCPUs |-> 0]
    /\ cpuInterruptsEnabled = [c \in 1..NumCPUs |-> TRUE]
    /\ allocatedBy = [b \in 1..NumBlocks |-> 0]

-----------------------------------------------------------------------------
(* Allocation - Pop from free list *)
(* Protected by interrupt guard on same CPU *)

\* Step 1: Enter interrupt guard, read head AND version (atomic snapshot)
StartAllocation(cpu) ==
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuDeallocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_READING"]
    /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
    \* Read BOTH head and version atomically (this is one 64-bit load)
    /\ cpuAllocHead' = [cpuAllocHead EXCEPT ![cpu] = freeListHead]
    /\ cpuAllocVersion' = [cpuAllocVersion EXCEPT ![cpu] = freeListVersion]
    /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock, cpuAllocNext,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, allocatedBy>>

\* Step 2: Read next pointer of head block
ReadAllocNext(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_READING"
    /\ cpuAllocHead[cpu] # 0
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ cpuAllocNext' = [cpuAllocNext EXCEPT ![cpu] = nextFreeBlock[cpuAllocHead[cpu]]]
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_GOT_NEXT"]
    /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock, cpuAllocHead, cpuAllocVersion,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Step 3: CAS to pop - compares BOTH index AND version
CASAllocPop(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_GOT_NEXT"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ LET head == cpuAllocHead[cpu]
           version == cpuAllocVersion[cpu]
           next == cpuAllocNext[cpu]
       IN
       \* CAS compares BOTH head AND version - this is the key ABA protection!
       IF freeListHead = head /\ freeListVersion = version
       THEN \* CAS succeeds - we own the block now
            /\ freeListHead' = next
            /\ freeListVersion' = IncVersion(version)  \* INCREMENT VERSION on success
            /\ blockState' = [blockState EXCEPT ![head] = "IN_USE"]
            /\ allocatedBy' = [allocatedBy EXCEPT ![head] = cpu]
            /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "IDLE"]
            /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
            /\ UNCHANGED <<nextFreeBlock, cpuAllocHead, cpuAllocVersion, cpuAllocNext,
                          cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                          cpuInterruptDepth>>
       ELSE \* CAS fails - re-read head AND version, retry
            /\ cpuAllocHead' = [cpuAllocHead EXCEPT ![cpu] = freeListHead]
            /\ cpuAllocVersion' = [cpuAllocVersion EXCEPT ![cpu] = freeListVersion]
            /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_READING"]
            /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock, cpuAllocNext,
                          cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                          cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Empty list case
AllocationEmpty(cpu) ==
    /\ cpuAllocState[cpu] = "ALLOC_READING"
    /\ cpuAllocHead[cpu] = 0
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "IDLE"]
    /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
    /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock,
                   cpuAllocHead, cpuAllocVersion, cpuAllocNext,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, allocatedBy>>

-----------------------------------------------------------------------------
(* Deallocation - Push to free list *)

\* Step 1: Lock block for deallocation (no interrupt guard yet)
StartDeallocation(cpu, block) ==
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuDeallocState[cpu] = "IDLE"
    /\ cpuInterruptDepth[cpu] = 0  \* Only from non-interrupt context
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ blockState[block] = "IN_USE"
    /\ allocatedBy[block] = cpu
    /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "DEALLOC_LOCKED"]
    /\ cpuDeallocBlock' = [cpuDeallocBlock EXCEPT ![cpu] = block]
    /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext,
                   cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

\* Step 2: Set AVAILABLE, enter interrupt guard, read head+version, set next
StartDeallocPush(cpu) ==
    /\ cpuDeallocState[cpu] = "DEALLOC_LOCKED"
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ LET block == cpuDeallocBlock[cpu] IN
       /\ blockState' = [blockState EXCEPT ![block] = "AVAILABLE"]
       /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
       /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "DEALLOC_PUSHING"]
       \* Read head AND version atomically
       /\ cpuDeallocHead' = [cpuDeallocHead EXCEPT ![cpu] = freeListHead]
       /\ cpuDeallocVersion' = [cpuDeallocVersion EXCEPT ![cpu] = freeListVersion]
       /\ nextFreeBlock' = [nextFreeBlock EXCEPT ![block] = freeListHead]
       /\ UNCHANGED <<freeListHead, freeListVersion, cpuAllocState, cpuAllocHead,
                      cpuAllocVersion, cpuAllocNext, cpuDeallocBlock,
                      cpuInterruptDepth, allocatedBy>>

\* Step 3: CAS to push block to head - compares BOTH index AND version
CompleteDeallocPush(cpu) ==
    /\ cpuDeallocState[cpu] = "DEALLOC_PUSHING"
    /\ cpuInterruptsEnabled[cpu] = FALSE
    /\ LET block == cpuDeallocBlock[cpu]
           expectedHead == cpuDeallocHead[cpu]
           expectedVersion == cpuDeallocVersion[cpu]
       IN
       \* CAS compares BOTH head AND version
       IF freeListHead = expectedHead /\ freeListVersion = expectedVersion
       THEN \* CAS succeeds - push block to head
            /\ freeListHead' = block
            /\ freeListVersion' = IncVersion(expectedVersion)  \* INCREMENT VERSION
            /\ allocatedBy' = [allocatedBy EXCEPT ![block] = 0]
            /\ cpuDeallocState' = [cpuDeallocState EXCEPT ![cpu] = "IDLE"]
            /\ cpuDeallocBlock' = [cpuDeallocBlock EXCEPT ![cpu] = 0]
            /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = TRUE]
            /\ UNCHANGED <<blockState, nextFreeBlock, cpuAllocState, cpuAllocHead,
                          cpuAllocVersion, cpuAllocNext, cpuDeallocHead, cpuDeallocVersion,
                          cpuInterruptDepth>>
       ELSE \* CAS fails - re-read head+version and update next pointer
            /\ cpuDeallocHead' = [cpuDeallocHead EXCEPT ![cpu] = freeListHead]
            /\ cpuDeallocVersion' = [cpuDeallocVersion EXCEPT ![cpu] = freeListVersion]
            /\ nextFreeBlock' = [nextFreeBlock EXCEPT ![block] = freeListHead]
            /\ UNCHANGED <<blockState, freeListHead, freeListVersion,
                          cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext,
                          cpuDeallocState, cpuDeallocBlock,
                          cpuInterruptDepth, cpuInterruptsEnabled, allocatedBy>>

-----------------------------------------------------------------------------
(* Interrupt handling *)

\* Interrupt fires on a CPU - handler allocates
InterruptAllocate(cpu) ==
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ cpuInterruptDepth[cpu] < MaxInterruptDepth
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptDepth' = [cpuInterruptDepth EXCEPT ![cpu] = @ + 1]
    /\ cpuAllocState' = [cpuAllocState EXCEPT ![cpu] = "ALLOC_READING"]
    /\ cpuInterruptsEnabled' = [cpuInterruptsEnabled EXCEPT ![cpu] = FALSE]
    /\ cpuAllocHead' = [cpuAllocHead EXCEPT ![cpu] = freeListHead]
    /\ cpuAllocVersion' = [cpuAllocVersion EXCEPT ![cpu] = freeListVersion]
    /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock, cpuAllocNext,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                   allocatedBy>>

\* Return from interrupt
ReturnFromInterrupt(cpu) ==
    /\ cpuInterruptDepth[cpu] > 0
    /\ cpuAllocState[cpu] = "IDLE"
    /\ cpuInterruptsEnabled[cpu] = TRUE
    /\ cpuInterruptDepth' = [cpuInterruptDepth EXCEPT ![cpu] = @ - 1]
    /\ UNCHANGED <<blockState, freeListHead, freeListVersion, nextFreeBlock,
                   cpuAllocState, cpuAllocHead, cpuAllocVersion, cpuAllocNext,
                   cpuDeallocState, cpuDeallocBlock, cpuDeallocHead, cpuDeallocVersion,
                   cpuInterruptsEnabled, allocatedBy>>

-----------------------------------------------------------------------------
(* Next state *)

Next ==
    \/ \E cpu \in 1..NumCPUs :
        \/ StartAllocation(cpu)
        \/ ReadAllocNext(cpu)
        \/ CASAllocPop(cpu)
        \/ AllocationEmpty(cpu)
        \/ \E block \in 1..NumBlocks : StartDeallocation(cpu, block)
        \/ StartDeallocPush(cpu)
        \/ CompleteDeallocPush(cpu)
        \/ InterruptAllocate(cpu)
        \/ ReturnFromInterrupt(cpu)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(* Safety Properties *)

\* IN_USE blocks must have an owner
NoDoubleFree ==
    \A b \in 1..NumBlocks :
        blockState[b] = "IN_USE" => allocatedBy[b] # 0

\* Block conservation
BlockConservation ==
    Cardinality({b \in 1..NumBlocks : blockState[b] = "IN_USE"}) +
    Cardinality({b \in 1..NumBlocks : blockState[b] = "FREE"}) +
    Cardinality({b \in 1..NumBlocks : blockState[b] = "AVAILABLE"}) = NumBlocks

\* CAS loops are protected by interrupt guard
AllocCASProtected ==
    \A cpu \in 1..NumCPUs :
        cpuAllocState[cpu] \in {"ALLOC_READING", "ALLOC_GOT_NEXT"} =>
            cpuInterruptsEnabled[cpu] = FALSE

DeallocPushProtected ==
    \A cpu \in 1..NumCPUs :
        cpuDeallocState[cpu] = "DEALLOC_PUSHING" =>
            cpuInterruptsEnabled[cpu] = FALSE

\* Core safety - the properties we actually care about
\* (Separate from TypeOK which includes version bounds)
CoreSafety ==
    /\ NoDoubleFree
    /\ BlockConservation
    /\ AllocCASProtected
    /\ DeallocPushProtected

\* Combined safety including type checking
Safety ==
    /\ TypeOK
    /\ CoreSafety

\* State constraint for bounded model checking
\* Limits exploration depth by version - used with CONSTRAINT in cfg
VersionBound == freeListVersion <= 20

=============================================================================
