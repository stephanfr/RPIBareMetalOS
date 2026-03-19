---------------------------- MODULE LockfreeBitset ----------------------------
(***************************************************************************)
(* TLA+ specification of the lock-free bitset class.                      *)
(*                                                                        *)
(* Models the atomic fetch_or / fetch_and based acquire/release           *)
(* operations on a shared atomic word where each bit can be independently *)
(* acquired and released.                                                 *)
(*                                                                        *)
(* C++ operation modeled (acquire):                                       *)
(*   uint64_t old = words_[w].fetch_or(mask, acquire);                    *)
(*   return (old & mask) ? ALREADY_ACQUIRED : SUCCESS;                    *)
(*                                                                        *)
(* C++ operation modeled (release):                                       *)
(*   uint64_t old = words_[w].fetch_and(~mask, release);                  *)
(*   return ((old & mask) == 0) ? ALREADY_RELEASED : SUCCESS;             *)
(*                                                                        *)
(* Key properties of fetch_or / fetch_and:                                *)
(* - Single atomic RMW instruction (LDSET/LDCLR on ARM64 with LSE)       *)
(* - No CAS loop, no retry, no back_off                                  *)
(* - Immune to contention from unrelated bits in the same word            *)
(* - Cannot be preempted mid-operation (hardware atomicity guarantee)     *)
(*                                                                        *)
(* Since each operation is a single atomic step, there are no             *)
(* intermediate states for interrupts to preempt. The interrupt model     *)
(* verifies that interleaving complete operations from thread context     *)
(* and ISR context (including on the same bit) preserves all invariants.  *)
(*                                                                        *)
(* Since bits in different words use different atomic variables and never  *)
(* contend, modeling a single word captures all interesting               *)
(* interleavings. Multi-word is just independent replication.             *)
(***************************************************************************)

EXTENDS Integers, FiniteSets

CONSTANTS
    NumBits,            \* Number of bits in the modeled word
    NumCPUs,            \* Number of CPUs (cores)
    MaxInterruptDepth   \* Maximum interrupt nesting depth (0 = no interrupts)

VARIABLES
    \* ===== SHARED ATOMIC WORD =====
    \* Models one atomic<uint64_t> from the bitset.
    bits,               \* [1..NumBits -> BOOLEAN]

    \* ===== PER-CPU EXECUTION DEPTH =====
    \* Tracks interrupt nesting. Depth 0 = thread context.
    \* Depth d > 0 = interrupt handler at nesting level d.
    activeDepth,        \* [1..NumCPUs -> 0..MaxInterruptDepth]

    \* ===== OWNERSHIP TRACKING (verification only) =====
    \* NOT part of the C++ implementation. Tracks which CPU successfully
    \* acquired each bit, enabling mutual exclusion verification.
    bitOwner            \* [1..NumBits -> 0..NumCPUs]

DepthRange == 0..MaxInterruptDepth

vars == <<bits, activeDepth, bitOwner>>

-----------------------------------------------------------------------------
(* Type invariant *)

TypeOK ==
    /\ bits \in [1..NumBits -> BOOLEAN]
    /\ activeDepth \in [1..NumCPUs -> DepthRange]
    /\ bitOwner \in [1..NumBits -> 0..NumCPUs]

-----------------------------------------------------------------------------
(* Initialization *)

Init ==
    /\ bits = [b \in 1..NumBits |-> FALSE]
    /\ activeDepth = [c \in 1..NumCPUs |-> 0]
    /\ bitOwner = [b \in 1..NumBits |-> 0]

-----------------------------------------------------------------------------
(* ACQUIRE OPERATION                                                       *)
(*                                                                         *)
(* Models: old = fetch_or(mask, acquire); return (old & mask) ? FAIL : OK  *)
(*                                                                         *)
(* fetch_or is a single atomic RMW. It reads the old word value, writes    *)
(* old | mask, and returns old. If the target bit was already set, the     *)
(* word is unchanged (1 OR 1 = 1) and the acquire fails. If the bit was   *)
(* clear, it is now set and the acquire succeeds.                          *)
(*                                                                         *)
(* Failed acquires (bit already set) are no-ops that produce no state      *)
(* change, so they are not modeled as explicit transitions.                *)

Acquire(cpu, bit) ==
    /\ bits[bit] = FALSE
    /\ bits' = [bits EXCEPT ![bit] = TRUE]
    /\ bitOwner' = [bitOwner EXCEPT ![bit] = cpu]
    /\ UNCHANGED activeDepth

-----------------------------------------------------------------------------
(* RELEASE OPERATION                                                       *)
(*                                                                         *)
(* Models: old = fetch_and(~mask, release); return !(old & mask) ? FAIL:OK *)
(*                                                                         *)
(* fetch_and is a single atomic RMW. It reads the old word value, writes   *)
(* old & ~mask, and returns old. If the target bit was set, it is now      *)
(* cleared and the release succeeds. If already clear, the word is         *)
(* unchanged (0 AND ~mask = 0) and the release fails.                      *)
(*                                                                         *)
(* Note: any CPU can release any acquired bit (the bitset does not         *)
(* enforce ownership). The hardware atomicity provides correctness.        *)

Release(cpu, bit) ==
    /\ bits[bit] = TRUE
    /\ bits' = [bits EXCEPT ![bit] = FALSE]
    /\ bitOwner' = [bitOwner EXCEPT ![bit] = 0]
    /\ UNCHANGED activeDepth

-----------------------------------------------------------------------------
(* INTERRUPT HANDLING                                                      *)
(*                                                                         *)
(* The bitset uses NO interrupt guards. With fetch_or/fetch_and, each     *)
(* operation is a single atomic instruction that cannot be preempted.      *)
(* Interrupts can only fire BETWEEN operations, not during them.           *)
(*                                                                         *)
(* The interrupt model verifies that interleaving ISR operations with      *)
(* thread-level operations preserves all safety invariants, including      *)
(* the scenario where an ISR operates on the same bit as the interrupted   *)
(* thread was about to operate on.                                         *)

\* Fire an interrupt on a CPU.
FireInterrupt(cpu) ==
    /\ activeDepth[cpu] < MaxInterruptDepth
    /\ activeDepth' = [activeDepth EXCEPT ![cpu] = @ + 1]
    /\ UNCHANGED <<bits, bitOwner>>

\* Return from interrupt.
ReturnFromInterrupt(cpu) ==
    /\ activeDepth[cpu] > 0
    /\ activeDepth' = [activeDepth EXCEPT ![cpu] = @ - 1]
    /\ UNCHANGED <<bits, bitOwner>>

-----------------------------------------------------------------------------
(* Next state relation *)

Next ==
    \E cpu \in 1..NumCPUs :
        \/ \E bit \in 1..NumBits :
            \/ Acquire(cpu, bit)
            \/ Release(cpu, bit)
        \/ FireInterrupt(cpu)
        \/ ReturnFromInterrupt(cpu)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(* SAFETY PROPERTIES                                                       *)
(*                                                                         *)
(* These invariants verify that the fetch_or / fetch_and based bitset     *)
(* maintains consistency under all interleavings of multi-CPU execution   *)
(* and interrupt preemption.                                               *)

\* 1. Ownership implies set: if a CPU owns a bit, that bit must be set.
BitOwnershipConsistent ==
    \A b \in 1..NumBits :
        bitOwner[b] # 0 => bits[b] = TRUE

\* 2. Set implies ownership: if a bit is set, exactly one CPU owns it.
\*    Together with (1), gives: bits[b] = TRUE <=> bitOwner[b] # 0
SetBitsHaveOwner ==
    \A b \in 1..NumBits :
        bits[b] = TRUE => bitOwner[b] # 0

\* 3. Interrupt depth stays within bounds.
InterruptDepthValid ==
    \A cpu \in 1..NumCPUs :
        activeDepth[cpu] \in DepthRange

\* Combined safety invariant
Safety ==
    /\ TypeOK
    /\ BitOwnershipConsistent
    /\ SetBitsHaveOwner
    /\ InterruptDepthValid

\* Core safety (without TypeOK)
CoreSafety ==
    /\ BitOwnershipConsistent
    /\ SetBitsHaveOwner

=============================================================================