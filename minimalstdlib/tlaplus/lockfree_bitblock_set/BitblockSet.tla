--------------------------- MODULE BitblockSet ---------------------------
(***************************************************************************)
(* TLA+ specification of the bitblock_set lock-free data structure.       *)
(*                                                                        *)
(* Unlike the single-bit bitset (which uses one atomic RMW per op),       *)
(* bitblock_set supports MULTI-BIT and MULTI-WORD acquire/release:        *)
(*                                                                        *)
(*   Single-word, 1 bit:  fetch_or / fetch_and  (1 atomic step)          *)
(*   Single-word, N bits: CAS loop              (1 atomic step)          *)
(*   Multi-word:          TWO fetch_or/and ops  (2 atomic steps!)         *)
(*                                                                        *)
(* CAS loops linearize to a single atomic step for safety analysis.       *)
(*                                                                        *)
(* The multi-word path is the critical concern: between the two atomic    *)
(* operations, the data structure is in an intermediate state where       *)
(* another thread or ISR can interleave.                                  *)
(*                                                                        *)
(* C++ multi-word acquire:                                                *)
(*   old_first = words[sw].fetch_or(first_mask)     // step 1             *)
(*   if (old_first & first_mask) return FAIL         // no rollback!      *)
(*   old_last = words[ew].fetch_or(last_mask)        // step 2            *)
(*   if (old_last & last_mask) {                                          *)
(*       words[sw].fetch_and(~first_mask)            // rollback step 1   *)
(*       return FAIL                                                      *)
(*   }                                                                    *)
(*   return SUCCESS                                                       *)
(*                                                                        *)
(* C++ multi-word release:                                                *)
(*   old_first = words[sw].load()                    // step 1            *)
(*   old_last  = words[ew].load()                    // step 2            *)
(*   if (!check(old_first, old_last)) return FAIL    // TOCTOU check      *)
(*   words[sw].fetch_and(~first_mask)                // step 3            *)
(*   words[ew].fetch_and(~last_mask)                 // step 4            *)
(*   return SUCCESS                                                       *)
(*                                                                        *)
(* This model verifies safety under arbitrary interleaving of threads     *)
(* and interrupt handlers, including kernel reentrancy scenarios.         *)
(***************************************************************************)

EXTENDS Integers, FiniteSets

CONSTANTS
    BPW,                \* Bits per word (e.g. 3 or 4)
    NW,                 \* Number of words (e.g. 2)
    NumCPUs,            \* Number of CPUs / concurrent execution contexts
    MaxInterruptDepth,  \* Maximum interrupt nesting depth (0 = no interrupts)
    MaxBits,            \* Maximum contiguous bits per operation
    MaxOps              \* Maximum operations per execution context (bounds model)

TB == NW * BPW          \* Total bits

VARIABLES
    (*=== SHARED STATE (models the atomic words) ===*)
    words,              \* [0..NW-1 -> SUBSET 0..BPW-1]  set of LOCAL bit indices set

    (*=== GHOST: OWNERSHIP TRACKING (verification only) ===*)
    owned,              \* [Ctx -> SUBSET 0..TB-1]  global bit indices owned per context
    tentative,          \* [Ctx -> SUBSET 0..TB-1]  bits set but not yet confirmed (multi-word acq)

    (*=== PER-CPU interrupt depth ===*)
    activeDepth,        \* [1..NumCPUs -> 0..MaxInterruptDepth]

    (*=== PER EXECUTION-CONTEXT STATE ===*)
    pc,                 \* program counter
    sb, nb,             \* start bit, num bits for current operation
    sW, eW,             \* start word, end word
    fM, lM,             \* first mask, last mask (sets of local bit indices)
    acqB,               \* global bit set for current operation
    firstGlob,          \* global bits in first word of multi-word op
    lastGlob,           \* global bits in last word of multi-word op
    savedOld,           \* saved old-value from fetch_or on first word
    ops                 \* operations completed

-----------------------------------------------------------------------------
(* Execution contexts: each CPU×depth is a separate context *)

Ctx == {<<cpu, depth>> : cpu \in 1..NumCPUs, depth \in 0..MaxInterruptDepth}

(*=== Helper operators ===*)

WOf(b) == b \div BPW
LB(b)  == b % BPW
RangeBits(s, n) == {s + i : i \in 0..(n-1)}

SingleWord(s, n) == WOf(s) = WOf(s + n - 1)

BitsInFirst(s, n) == IF SingleWord(s, n) THEN n ELSE BPW - LB(s)
BitsInLast(s, n)  == n - BitsInFirst(s, n)

FMaskOf(s, n) == {LB(s) + i : i \in 0..(BitsInFirst(s, n) - 1)}
LMaskOf(s, n) == IF SingleWord(s, n) THEN {}
                 ELSE {i : i \in 0..(BitsInLast(s, n) - 1)}

FirstGlobalBits(s, n) == {s + i : i \in 0..(BitsInFirst(s, n) - 1)}
LastGlobalBits(s, n)  == IF SingleWord(s, n) THEN {}
                         ELSE {s + BitsInFirst(s, n) + i : i \in 0..(BitsInLast(s, n) - 1)}

vars == <<words, owned, tentative, activeDepth,
          pc, sb, nb, sW, eW, fM, lM, acqB, firstGlob, lastGlob, savedOld, ops>>

ASSUME BPW \in 2..8
ASSUME NW \in 1..4
ASSUME NumCPUs >= 1
ASSUME MaxInterruptDepth >= 0
ASSUME MaxBits >= 1
ASSUME MaxOps >= 1

-----------------------------------------------------------------------------
(* Type invariant *)

TypeOK ==
    /\ words \in [0..(NW-1) -> SUBSET (0..(BPW-1))]
    /\ \A c \in Ctx : owned[c] \subseteq 0..(TB-1)
    /\ \A c \in Ctx : tentative[c] \subseteq 0..(TB-1)
    /\ activeDepth \in [1..NumCPUs -> 0..MaxInterruptDepth]

-----------------------------------------------------------------------------
(* SAFETY PROPERTIES *)

(* 1. Mutual exclusion: no two contexts own the same bit *)
MutualExclusion ==
    \A c1, c2 \in Ctx : c1 # c2 => owned[c1] \cap owned[c2] = {}

(* 2. Every owned bit is set in hardware *)
OwnedImpliesSet ==
    \A c \in Ctx : \A b \in owned[c] : LB(b) \in words[WOf(b)]

(* 3. No orphaned bits: every set bit is accounted for *)
(*    (owned by someone, or tentatively held during multi-word acquire) *)
NoOrphanBits ==
    \A w \in 0..(NW-1) : \A lb \in words[w] :
        LET gb == w * BPW + lb IN
        \/ \E c \in Ctx : gb \in owned[c]
        \/ \E c \in Ctx : gb \in tentative[c]

(* 4. Tentative bits don't overlap with owned bits of other contexts *)
TentativeSafe ==
    \A c1, c2 \in Ctx : c1 # c2 =>
        /\ tentative[c1] \cap owned[c2] = {}
        /\ tentative[c1] \cap tentative[c2] = {}

(* 5. Interrupt depth stays in bounds *)
InterruptDepthValid ==
    \A cpu \in 1..NumCPUs : activeDepth[cpu] \in 0..MaxInterruptDepth

(* Combined safety invariant *)
Safety ==
    /\ TypeOK
    /\ MutualExclusion
    /\ OwnedImpliesSet
    /\ InterruptDepthValid

(* Strict safety includes orphan check *)
StrictSafety ==
    /\ Safety
    /\ NoOrphanBits
    /\ TentativeSafe

-----------------------------------------------------------------------------
(* Initialization *)

Init ==
    /\ words = [w \in 0..(NW-1) |-> {}]
    /\ owned = [c \in Ctx |-> {}]
    /\ tentative = [c \in Ctx |-> {}]
    /\ activeDepth = [cpu \in 1..NumCPUs |-> 0]
    /\ pc = [c \in Ctx |-> "idle"]
    /\ sb = [c \in Ctx |-> 0]
    /\ nb = [c \in Ctx |-> 0]
    /\ sW = [c \in Ctx |-> 0]
    /\ eW = [c \in Ctx |-> 0]
    /\ fM = [c \in Ctx |-> {}]
    /\ lM = [c \in Ctx |-> {}]
    /\ acqB = [c \in Ctx |-> {}]
    /\ firstGlob = [c \in Ctx |-> {}]
    /\ lastGlob = [c \in Ctx |-> {}]
    /\ savedOld = [c \in Ctx |-> {}]
    /\ ops = [c \in Ctx |-> 0]

-----------------------------------------------------------------------------
(* HELPER: context is currently active (its CPU is at its depth) *)

IsActive(c) == activeDepth[c[1]] = c[2]

(* HELPER: unchanged for all per-context vars except the listed ones *)

CtxUnchanged(c) ==
    /\ pc' = [pc EXCEPT ![c] = pc[c]]
    /\ sb' = [sb EXCEPT ![c] = sb[c]]
    /\ nb' = [nb EXCEPT ![c] = nb[c]]
    /\ sW' = [sW EXCEPT ![c] = sW[c]]
    /\ eW' = [eW EXCEPT ![c] = eW[c]]
    /\ fM' = [fM EXCEPT ![c] = fM[c]]
    /\ lM' = [lM EXCEPT ![c] = lM[c]]
    /\ acqB' = [acqB EXCEPT ![c] = acqB[c]]
    /\ firstGlob' = [firstGlob EXCEPT ![c] = firstGlob[c]]
    /\ lastGlob' = [lastGlob EXCEPT ![c] = lastGlob[c]]
    /\ savedOld' = [savedOld EXCEPT ![c] = savedOld[c]]
    /\ ops' = [ops EXCEPT ![c] = ops[c]]

-----------------------------------------------------------------------------
(* ===================================================================== *)
(* ACQUIRE: SINGLE-WORD                                                  *)
(* Models both 1-bit (fetch_or) and N-bit (CAS loop).                   *)
(* Both linearize to a single atomic step.                               *)
(* ===================================================================== *)

AcquireSingleWord(c) ==
    /\ IsActive(c)
    /\ pc[c] = "acq_sw"
    /\ LET curWord == words[sW[c]] IN
       IF fM[c] \cap curWord # {} THEN
           \* Bits already taken - fail
           /\ pc' = [pc EXCEPT ![c] = "done"]
           /\ UNCHANGED <<words, owned, tentative>>
       ELSE
           \* Success: atomically set bits and take ownership
           /\ words' = [words EXCEPT ![sW[c]] = curWord \union fM[c]]
           /\ owned' = [owned EXCEPT ![c] = owned[c] \union acqB[c]]
           /\ pc' = [pc EXCEPT ![c] = "done"]
           /\ UNCHANGED tentative
    /\ UNCHANGED <<activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, savedOld, ops>>

-----------------------------------------------------------------------------
(* ===================================================================== *)
(* ACQUIRE: MULTI-WORD STEP 1 - fetch_or on first word                  *)
(*                                                                       *)
(* BUG IN C++: if (old_first & first_mask), the code returns FAIL        *)
(* without rolling back the bits it just set via fetch_or. Bits that     *)
(* were clear get set and are ORPHANED (set but unowned).                *)
(* ===================================================================== *)

AcquireMultiFirst(c) ==
    /\ IsActive(c)
    /\ pc[c] = "acq_mw1"
    /\ LET curWord == words[sW[c]]
           mask == fM[c]
       IN
       \* fetch_or: unconditionally set bits, get old value
       /\ savedOld' = [savedOld EXCEPT ![c] = curWord]
       /\ words' = [words EXCEPT ![sW[c]] = curWord \union mask]
       /\ IF mask \cap curWord # {} THEN
              \* FAIL: some bits were already set in the first word.
              \* BUG: fetch_or already set (mask \ curWord) bits -- they are now ORPHANED.
              \* The C++ code does NOT roll back these newly set bits.
              /\ pc' = [pc EXCEPT ![c] = "done"]
              /\ UNCHANGED <<owned, tentative>>
          ELSE
              \* First word acquired -- mark as tentative, proceed to second word
              /\ tentative' = [tentative EXCEPT ![c] = firstGlob[c]]
              /\ pc' = [pc EXCEPT ![c] = "acq_mw2"]
              /\ UNCHANGED owned
    /\ UNCHANGED <<activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, ops>>

-----------------------------------------------------------------------------
(* ===================================================================== *)
(* ACQUIRE: MULTI-WORD STEP 2 - fetch_or on last word                   *)
(*                                                                       *)
(* If this step fails, the code DOES roll back the first word.           *)
(* ===================================================================== *)

AcquireMultiLast(c) ==
    /\ IsActive(c)
    /\ pc[c] = "acq_mw2"
    /\ LET curWord == words[eW[c]]
           mask == lM[c]
       IN
       IF mask \cap curWord # {} THEN
           \* FAIL on last word.
           \* fetch_or on last word sets bits (some already set, some maybe not).
           \* C++ rolls back first word via fetch_and(~first_mask).
           \* C++ does NOT clean up newly set bits in last word.
           \* Model: first word cleared of fM, last word gets mask OR'd in.
           /\ words' = [words EXCEPT
                  ![sW[c]] = words[sW[c]] \ fM[c],
                  ![eW[c]] = curWord \union mask]
           /\ tentative' = [tentative EXCEPT ![c] = {}]
           /\ pc' = [pc EXCEPT ![c] = "done"]
           /\ UNCHANGED owned
       ELSE
           \* SUCCESS: fetch_or on last word sets bits (all were clear).
           \* Both words now have our bits set.
           /\ words' = [words EXCEPT ![eW[c]] = curWord \union mask]
           /\ owned' = [owned EXCEPT ![c] = owned[c] \union acqB[c]]
           /\ tentative' = [tentative EXCEPT ![c] = {}]
           /\ pc' = [pc EXCEPT ![c] = "done"]
    /\ UNCHANGED <<activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, savedOld, ops>>

-----------------------------------------------------------------------------
(* ===================================================================== *)
(* RELEASE: SINGLE-WORD                                                  *)
(* Models both 1-bit (fetch_and) and N-bit (CAS loop).                  *)
(* Both linearize to a single atomic step.                               *)
(* ===================================================================== *)

ReleaseSingleWord(c) ==
    /\ IsActive(c)
    /\ pc[c] = "rel_sw"
    /\ words' = [words EXCEPT ![sW[c]] = words[sW[c]] \ fM[c]]
    /\ owned' = [owned EXCEPT ![c] = owned[c] \ acqB[c]]
    /\ pc' = [pc EXCEPT ![c] = "done"]
    /\ UNCHANGED <<tentative, activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, savedOld, ops>>

-----------------------------------------------------------------------------
(* ===================================================================== *)
(* RELEASE: MULTI-WORD                                                   *)
(* Step 1: Load first word (for TOCTOU check)                           *)
(* Step 2: Load last word and check both                                *)
(* Step 3: fetch_and(~first_mask) on first word                         *)
(* Step 4: fetch_and(~last_mask) on last word                           *)
(* ===================================================================== *)

ReleaseMultiLoad(c) ==
    /\ IsActive(c)
    /\ pc[c] = "rel_mw_load"
    \* Load first word (save for check)
    /\ savedOld' = [savedOld EXCEPT ![c] = words[sW[c]]]
    /\ pc' = [pc EXCEPT ![c] = "rel_mw_check"]
    /\ UNCHANGED <<words, owned, tentative, activeDepth, sb, nb, sW, eW,
                   fM, lM, acqB, firstGlob, lastGlob, ops>>

ReleaseMultiCheck(c) ==
    /\ IsActive(c)
    /\ pc[c] = "rel_mw_check"
    \* Load last word and check both (C++ loads both then checks)
    /\ LET lastWord == words[eW[c]] IN
       IF ~(fM[c] \subseteq savedOld[c]) \/ ~(lM[c] \subseteq lastWord) THEN
           \* Check failed -- bits not all set (shouldn't happen with correct callers)
           /\ pc' = [pc EXCEPT ![c] = "done"]
           /\ UNCHANGED <<words, owned, tentative>>
       ELSE
           /\ pc' = [pc EXCEPT ![c] = "rel_mw_clear1"]
           /\ UNCHANGED <<words, owned, tentative>>
    /\ UNCHANGED <<activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, savedOld, ops>>

ReleaseMultiClear1(c) ==
    /\ IsActive(c)
    /\ pc[c] = "rel_mw_clear1"
    \* fetch_and(~first_mask): clear first word bits
    /\ words' = [words EXCEPT ![sW[c]] = words[sW[c]] \ fM[c]]
    \* Ghost: release ownership of first word's bits immediately
    /\ owned' = [owned EXCEPT ![c] = owned[c] \ firstGlob[c]]
    /\ pc' = [pc EXCEPT ![c] = "rel_mw_clear2"]
    /\ UNCHANGED <<tentative, activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, savedOld, ops>>

ReleaseMultiClear2(c) ==
    /\ IsActive(c)
    /\ pc[c] = "rel_mw_clear2"
    \* fetch_and(~last_mask): clear last word bits
    /\ words' = [words EXCEPT ![eW[c]] = words[eW[c]] \ lM[c]]
    \* Ghost: release ownership of last word's bits
    /\ owned' = [owned EXCEPT ![c] = owned[c] \ lastGlob[c]]
    /\ pc' = [pc EXCEPT ![c] = "done"]
    /\ UNCHANGED <<tentative, activeDepth, sb, nb, sW, eW, fM, lM,
                   acqB, firstGlob, lastGlob, savedOld, ops>>

-----------------------------------------------------------------------------
(* ===================================================================== *)
(* OPERATION DISPATCH: choose acquire or release, set up, dispatch       *)
(* ===================================================================== *)

StartAcquire(c, s, n) ==
    /\ IsActive(c)
    /\ pc[c] = "idle"
    /\ ops[c] < MaxOps
    /\ s >= 0 /\ n >= 1 /\ n <= MaxBits /\ s + n <= TB
    /\ sb' = [sb EXCEPT ![c] = s]
    /\ nb' = [nb EXCEPT ![c] = n]
    /\ sW' = [sW EXCEPT ![c] = WOf(s)]
    /\ eW' = [eW EXCEPT ![c] = WOf(s + n - 1)]
    /\ fM' = [fM EXCEPT ![c] = FMaskOf(s, n)]
    /\ lM' = [lM EXCEPT ![c] = LMaskOf(s, n)]
    /\ acqB' = [acqB EXCEPT ![c] = RangeBits(s, n)]
    /\ firstGlob' = [firstGlob EXCEPT ![c] = FirstGlobalBits(s, n)]
    /\ lastGlob' = [lastGlob EXCEPT ![c] = LastGlobalBits(s, n)]
    /\ IF SingleWord(s, n)
       THEN pc' = [pc EXCEPT ![c] = "acq_sw"]
       ELSE pc' = [pc EXCEPT ![c] = "acq_mw1"]
    /\ UNCHANGED <<words, owned, tentative, activeDepth, savedOld, ops>>

StartRelease(c, s, n) ==
    /\ IsActive(c)
    /\ pc[c] = "idle"
    /\ ops[c] < MaxOps
    /\ s >= 0 /\ n >= 1 /\ n <= MaxBits /\ s + n <= TB
    \* Can only release bits we actually own
    /\ RangeBits(s, n) \subseteq owned[c]
    /\ sb' = [sb EXCEPT ![c] = s]
    /\ nb' = [nb EXCEPT ![c] = n]
    /\ sW' = [sW EXCEPT ![c] = WOf(s)]
    /\ eW' = [eW EXCEPT ![c] = WOf(s + n - 1)]
    /\ fM' = [fM EXCEPT ![c] = FMaskOf(s, n)]
    /\ lM' = [lM EXCEPT ![c] = LMaskOf(s, n)]
    /\ acqB' = [acqB EXCEPT ![c] = RangeBits(s, n)]
    /\ firstGlob' = [firstGlob EXCEPT ![c] = FirstGlobalBits(s, n)]
    /\ lastGlob' = [lastGlob EXCEPT ![c] = LastGlobalBits(s, n)]
    /\ IF SingleWord(s, n)
       THEN pc' = [pc EXCEPT ![c] = "rel_sw"]
       ELSE pc' = [pc EXCEPT ![c] = "rel_mw_load"]
    /\ UNCHANGED <<words, owned, tentative, activeDepth, savedOld, ops>>

FinishOp(c) ==
    /\ IsActive(c)
    /\ pc[c] = "done"
    /\ ops' = [ops EXCEPT ![c] = ops[c] + 1]
    /\ pc' = [pc EXCEPT ![c] = "idle"]
    /\ UNCHANGED <<words, owned, tentative, activeDepth, sb, nb, sW, eW,
                   fM, lM, acqB, firstGlob, lastGlob, savedOld>>

-----------------------------------------------------------------------------
(* INTERRUPT HANDLING                                                    *)
(*                                                                       *)
(* The bitblock_set uses NO interrupt guards. Multi-word operations      *)
(* consist of multiple atomic steps. Between any two steps, an           *)
(* interrupt can fire and the ISR can itself call acquire/release on     *)
(* the same bitblock_set. This models that scenario.                    *)

FireInterrupt(cpu) ==
    /\ activeDepth[cpu] < MaxInterruptDepth
    /\ activeDepth' = [activeDepth EXCEPT ![cpu] = @ + 1]
    /\ UNCHANGED <<words, owned, tentative,
                   pc, sb, nb, sW, eW, fM, lM, acqB,
                   firstGlob, lastGlob, savedOld, ops>>

ReturnFromInterrupt(cpu) ==
    /\ activeDepth[cpu] > 0
    \* ISR must have completed its operation before returning
    /\ pc[<<cpu, activeDepth[cpu]>>] = "idle"
    /\ activeDepth' = [activeDepth EXCEPT ![cpu] = @ - 1]
    /\ UNCHANGED <<words, owned, tentative,
                   pc, sb, nb, sW, eW, fM, lM, acqB,
                   firstGlob, lastGlob, savedOld, ops>>

-----------------------------------------------------------------------------
(* Next state relation *)

Next ==
    \/ \E c \in Ctx :
        \/ \E s \in 0..(TB-1), n \in 1..MaxBits :
            \/ StartAcquire(c, s, n)
            \/ StartRelease(c, s, n)
        \/ AcquireSingleWord(c)
        \/ AcquireMultiFirst(c)
        \/ AcquireMultiLast(c)
        \/ ReleaseSingleWord(c)
        \/ ReleaseMultiLoad(c)
        \/ ReleaseMultiCheck(c)
        \/ ReleaseMultiClear1(c)
        \/ ReleaseMultiClear2(c)
        \/ FinishOp(c)
    \/ \E cpu \in 1..NumCPUs :
        \/ FireInterrupt(cpu)
        \/ ReturnFromInterrupt(cpu)

Spec == Init /\ [][Next]_vars

=============================================================================
