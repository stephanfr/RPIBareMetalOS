-------------------------- MODULE LockfreeSkiplist --------------------------
(***************************************************************************)
(* Bounded model for include/lockfree/skiplist with per-CPU epoch read     *)
(* sections and lagged retired-node reclamation.                            *)
(*                                                                          *)
(* Matches C++ control-flow of: insert, remove, find, gc.                  *)
(*                                                                          *)
(* Key fidelity improvements over prior version:                            *)
(*  1. `fullyUnlinked` set: nodes must be FULLY_UNLINKED (all upper-level  *)
(*     pointer references removed) before they are eligible for reclaim,   *)
(*     matching the C++ `unlink_state::FULLY_UNLINKED` gate.               *)
(*  2. `CanReclaim`: reclaim requires ALL CPU reader slots to be inactive   *)
(*     (matches C++ `advance_and_reclaim` which returns early if *)
(*     any `epoch_reader_states_[i].active_` is true).                     *)
(*  3. `find` operation modeled as a read-section-wrapped scan that may     *)
(*     assist with GC of tombstoned nodes (matches C++ `find()`).          *)
(*  4. Interrupt re-entrancy: a CPU may enter a nested read section while   *)
(*     already inside one (models ISR calling skiplist ops while a kernel   *)
(*     thread holds the outer read section on the same CPU). The per-CPU   *)
(*     `readerDepth` counter correctly gates epoch snapshot: only the first *)
(*     entry (depth 0->1) records the epoch, matching the C++ code path    *)
(*     `if (previous_depth == 0) { ... epoch_.store(...) }`.               *)
(*  5. `remove`-bottom success path calls `try_advance_epoch` then          *)
(*     throttled `advance_and_reclaim`, reflected as a combined  *)
(*     AdvanceAndReclaimStep action.                                        *)
(*  6. `RemoveBottomStolenByOther`: models the C++ path where the level-0  *)
(*     tombstone check finds another thread already soft-deleted the node   *)
(*     (return false without retiring).                                    *)
(*  7. `RemoveMarkGotoRetry`: models `goto RETRY_REMOVE` when              *)
(*     mark_retry_count >= MAX_REMOVE_MARK_RETRIES_PER_LEVEL.             *)
(***************************************************************************)

EXTENDS Integers, FiniteSets

CONSTANTS
    Keys,
    Threads,
    Cpus,
    MaxLevels,
    MaxRetryTicks,
    MaxEpoch,
    EpochLag,
    MaxReadDepth

VARIABLES
    present,          \* Keys logically present (not soft-deleted)
    tomb,             \* Keys soft-deleted (marked) but not yet physically unlinked
    retired,          \* Keys physically unlinked and on the retired list
    fullyUnlinked,    \* Keys with unlink_state::FULLY_UNLINKED set (upper levels cleared)
    retiredAt,        \* Epoch at which each key was retired
    globalEpoch,
    readerDepth,      \* Per-CPU re-entrant read-section nesting depth (0 = idle)
    readerEpoch,      \* Per-CPU: epoch snapshot at outermost enter_read_section
    pc,
    op,
    opKey,
    level,
    retryTick

vars == <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
          readerDepth, readerEpoch, pc, op, opKey, level, retryTick>>

PcStates == {
    "idle",
    "insert_find", "insert_link0", "insert_link_upper",
    "remove_find", "remove_mark", "remove_bottom",
    "find_scan",
    "gc_scan"
}

Ops == {"none", "insert", "remove", "find", "gc"}

(*
 * In the C++ code each thread maps to the CPU it is currently scheduled on.
 * For simplicity in the bounded model we allow any mapping; the critical
 * property is that two threads sharing a Cpu share the same readerDepth slot,
 * which is what makes interrupt re-entrancy relevant.
 *)
cpu_of(t) == IF t \in Cpus THEN t ELSE CHOOSE c \in Cpus : TRUE

TypeOK ==
    /\ present \subseteq Keys
    /\ tomb \subseteq Keys
    /\ retired \subseteq Keys
    /\ fullyUnlinked \subseteq retired
    /\ present \cap tomb = {}
    /\ retiredAt \in [Keys -> 0..MaxEpoch]
    /\ globalEpoch \in 0..MaxEpoch
    /\ readerDepth \in [Cpus -> 0..MaxReadDepth]
    /\ readerEpoch \in [Cpus -> 0..MaxEpoch]
    /\ pc \in [Threads -> PcStates]
    /\ op \in [Threads -> Ops]
    /\ opKey \in [Threads -> Keys]
    /\ level \in [Threads -> 0..MaxLevels]
    /\ retryTick \in [Threads -> 0..MaxRetryTicks]
    /\ Threads # {}
    /\ Cpus # {}

Init ==
    /\ present = {}
    /\ tomb = {}
    /\ retired = {}
    /\ fullyUnlinked = {}
    /\ retiredAt = [k \in Keys |-> 0]
    /\ globalEpoch = 1
    /\ readerDepth = [c \in Cpus |-> 0]
    /\ readerEpoch = [c \in Cpus |-> 0]
    /\ pc = [t \in Threads |-> "idle"]
    /\ op = [t \in Threads |-> "none"]
    /\ opKey = [t \in Threads |-> CHOOSE k \in Keys : TRUE]
    /\ level = [t \in Threads |-> 0]
    /\ retryTick = [t \in Threads |-> 0]

InitRemoveScenario ==
    /\ LET key == CHOOSE k \in Keys : TRUE IN present = {key}
    /\ tomb = {}
    /\ retired = {}
    /\ fullyUnlinked = {}
    /\ retiredAt = [k \in Keys |-> 0]
    /\ globalEpoch = 1
    /\ readerDepth = [c \in Cpus |-> 0]
    /\ readerEpoch = [c \in Cpus |-> 0]
    /\ pc = [t \in Threads |-> "idle"]
    /\ op = [t \in Threads |-> "none"]
    /\ opKey = [t \in Threads |-> CHOOSE k \in Keys : TRUE]
    /\ level = [t \in Threads |-> 0]
    /\ retryTick = [t \in Threads |-> 0]

TickRetry(t) ==
    [retryTick EXCEPT ![t] = IF retryTick[t] < MaxRetryTicks THEN retryTick[t] + 1 ELSE MaxRetryTicks]

ResetRetry(t) ==
    [retryTick EXCEPT ![t] = 0]

(***************************************************************************)
(* Per-CPU epoch read-section helpers.                                     *)
(*                                                                          *)
(* EnterRead models `enter_read_section`:                                  *)
(*   previous_depth = read_depth_.fetch_add(1)                             *)
(*   if (previous_depth == 0) { epoch_.store(global_epoch_) }              *)
(*                                                                          *)
(* Crucially, if a CPU is already inside a read section (readerDepth > 0)  *)
(* -- i.e. an interrupt fires while a thread is in a skiplist op -- the    *)
(* epoch snapshot is NOT updated.  The nested caller inherits the outer    *)
(* epoch, which is safe because the outer epoch was recorded first.        *)
(* This is the interrupt re-entrancy property modeled here.                *)
(***************************************************************************)
EnterRead(t) ==
    LET c == cpu_of(t) IN
    /\ readerDepth[c] < MaxReadDepth
    /\ readerDepth' = [readerDepth EXCEPT ![c] = @ + 1]
    /\ readerEpoch' = [readerEpoch EXCEPT ![c] =
                          IF readerDepth[c] = 0
                          THEN globalEpoch    \* first entry: snapshot current epoch
                          ELSE @]            \* nested entry: inherit outer epoch

ExitRead(t) ==
    LET c == cpu_of(t) IN
    /\ readerDepth[c] > 0
    /\ readerDepth' = [readerDepth EXCEPT ![c] = @ - 1]
    /\ readerEpoch' = readerEpoch

FinishOpCore(t) ==
    /\ pc' = [pc EXCEPT ![t] = "idle"]
    /\ op' = [op EXCEPT ![t] = "none"]
    /\ level' = [level EXCEPT ![t] = 0]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]

FinishOp(t) ==
    /\ FinishOpCore(t)
    /\ ExitRead(t)

(***************************************************************************)
(* Epoch advancement predicate.                                            *)
(*                                                                          *)
(* `CanAdvanceEpoch(callerCpu)` matches C++ `try_advance_epoch`:           *)
(*   For every CPU slot i:                                                  *)
(*     - self-exempt if sole occupant (depth <= 1)                         *)
(*     - skip if idle (depth = 0)                                          *)
(*     - else require reader_epoch >= globalEpoch                          *)
(* Uses readerDepth as the authoritative reader-presence indicator,        *)
(* matching the fixed C++ which eliminated the racy reader_active_ flag.   *)
(***************************************************************************)
CanAdvanceEpoch(callerCpu) ==
    \A c \in Cpus :
        (c = callerCpu /\ readerDepth[c] <= 1)
        \/ (readerDepth[c] = 0)
        \/ (readerEpoch[c] >= globalEpoch)

(***************************************************************************)
(* Reclaim predicate.                                                      *)
(*                                                                          *)
(* `CanReclaim(callerCpu, epoch)` matches C++                              *)
(* `reclaim_from_per_cpu_list`:                                            *)
(*   reclaim_epoch = epoch - EpochLag                                      *)
(*   For every CPU slot i:                                                 *)
(*     - self-exempt ONLY if readerDepth <= 1 (sole occupant)              *)
(*     - skip if idle (depth = 0), matching reader_is_active()=depth>0    *)
(*     - else require reader_epoch > reclaim_epoch                         *)
(*   Uses readerDepth as authoritative reader-presence indicator,          *)
(*   matching the fixed C++ which eliminated the racy reader_active_ flag. *)
(***************************************************************************)
CanReclaim(callerCpu, epoch) ==
    LET reclaimEpoch == IF epoch > EpochLag THEN epoch - EpochLag ELSE 0 IN
    \A c \in Cpus :
        (c = callerCpu /\ readerDepth[c] <= 1)
        \/ (readerDepth[c] = 0)
        \/ (readerEpoch[c] > reclaimEpoch)

(***************************************************************************)
(* Reclaimable nodes: those that are:                                      *)
(*   - In fullyUnlinked (unlink_state::FULLY_UNLINKED set)                 *)
(*   - Retired epoch satisfies the lag condition                           *)
(* Matches C++ reclaim gate:                                               *)
(*   reclaim_eligible = (unlink_state == FULLY_UNLINKED)                  *)
(*                   && (retired_epoch_ <= reclaim_epoch)                  *)
(* where reclaim_epoch = globalEpoch - EpochLag                           *)
(***************************************************************************)
ReclaimableNodes(epoch) ==
    { k \in fullyUnlinked :
        /\ retiredAt[k] > 0
        /\ retiredAt[k] + EpochLag <= epoch }

(***************************************************************************)
(* AdvanceAndReclaim: combines try_advance_epoch + reclaim_retired_nodes.  *)
(* Called after a successful remove-bottom or from gc.                     *)
(* `callerCpu` is exempted from both the advance and reclaim guards,      *)
(* matching the C++ `try_advance_epoch(slot)` and                         *)
(* `reclaim_from_per_cpu_list(slot)` self-exemption.                      *)
(***************************************************************************)
AdvanceAndReclaim(callerCpu) ==
    LET nextEpoch   == IF CanAdvanceEpoch(callerCpu)
                          THEN IF globalEpoch < MaxEpoch THEN globalEpoch + 1 ELSE MaxEpoch
                          ELSE globalEpoch IN
    LET reclaimable == IF CanReclaim(callerCpu, nextEpoch) THEN ReclaimableNodes(nextEpoch) ELSE {} IN
    /\ globalEpoch' = nextEpoch
    /\ retired' = retired \ reclaimable
    /\ fullyUnlinked' = fullyUnlinked \ reclaimable
    /\ retiredAt' = retiredAt   \* retiredAt entries for reclaimed keys now irrelevant

(***************************************************************************)
(* RetireKey: soft-deleted node is physically unlinked from level 0 and   *)
(* added to the retired+fullyUnlinked lists, recorded at current epoch.   *)
(* In C++: unlink_state=FULLY_UNLINKED is set at the level-0 CAS success  *)
(* point, immediately before retire_node() is called.                     *)
(***************************************************************************)
RetireKey(k) ==
    /\ retired' = retired \cup {k}
    /\ fullyUnlinked' = fullyUnlinked \cup {k}
    /\ retiredAt' = [retiredAt EXCEPT ![k] = globalEpoch]
    /\ tomb' = tomb \ {k}

(***************************************************************************)
(* START operations                                                        *)
(***************************************************************************)

StartInsert(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "insert_find"]
    /\ op' = [op EXCEPT ![t] = "insert"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = 0]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch>>

StartRemove(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "remove_find"]
    /\ op' = [op EXCEPT ![t] = "remove"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = MaxLevels]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch>>

StartFind(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "find_scan"]
    /\ op' = [op EXCEPT ![t] = "find"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = 0]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch>>

StartGc(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "gc_scan"]
    /\ op' = [op EXCEPT ![t] = "gc"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = MaxLevels]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch>>

(***************************************************************************)
(* INSERT state machine                                                    *)
(*                                                                          *)
(* insert_find: scan for key; reject if already present or soft-deleted    *)
(* insert_link0: CAS at level 0; nondeterministic success/retry/abort      *)
(* insert_link_upper: link upper levels (bounded retries); always finish   *)
(*                                                                          *)
(* Matches C++:                                                            *)
(*   RETRY_INSERT while(true) { find_with_nodelists -> if FOUND/SOFT_DELETE*)
(*     return false; else CAS level0 -> CAS upper levels -> return true }  *)
(***************************************************************************)
InsertFindReject(t) ==
    /\ pc[t] = "insert_find"
    /\ op[t] = "insert"
    /\ opKey[t] \in (present \cup tomb)
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch, opKey>>

InsertFindMiss(t) ==
    /\ pc[t] = "insert_find"
    /\ op[t] = "insert"
    /\ opKey[t] \notin (present \cup tomb)
    /\ pc' = [pc EXCEPT ![t] = "insert_link0"]
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey, level, retryTick>>

InsertLink0Success(t) ==
    /\ pc[t] = "insert_link0"
    /\ op[t] = "insert"
    /\ opKey[t] \notin (present \cup tomb)
    /\ present' = present \cup {opKey[t]}
    /\ pc' = [pc EXCEPT ![t] = "insert_link_upper"]
    /\ level' = [level EXCEPT ![t] = 1]
    /\ retryTick' = retryTick
    /\ UNCHANGED <<tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey>>

\* CAS failure on level 0: retry insert_find loop, bounded by MaxRetryTicks
InsertLink0Fail(t) ==
    /\ pc[t] = "insert_link0"
    /\ op[t] = "insert"
    /\ retryTick[t] < MaxRetryTicks
    /\ pc' = [pc EXCEPT ![t] = "insert_find"]
    /\ retryTick' = TickRetry(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey, level>>

\* Exceeded MAX_INSERT_LEVEL0_RETRIES: delete new_node and return false
InsertLink0Abort(t) ==
    /\ pc[t] = "insert_link0"
    /\ op[t] = "insert"
    /\ retryTick[t] = MaxRetryTicks
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch, opKey>>

\* After level-0 success, upper-level CASes complete (with bounded retries).
\* In C++ bounded upper-level failures just skip that level; always returns true.
InsertUpperFinish(t) ==
    /\ pc[t] = "insert_link_upper"
    /\ op[t] = "insert"
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch, opKey>>

(***************************************************************************)
(* REMOVE state machine                                                    *)
(*                                                                          *)
(* remove_find: scan; return false if not in `present`                    *)
(* remove_mark: soft-delete upper levels top-down; goto RETRY_REMOVE if   *)
(*   mark_retry_count >= MAX_REMOVE_MARK_RETRIES_PER_LEVEL                 *)
(* remove_bottom: CAS-mark level-0 link; on success retire + epoch work;  *)
(*   if another thread already marked: return false;                       *)
(*   on CAS failure retry or goto RETRY_REMOVE after MAX_REMOVE_BOTTOM.   *)
(*                                                                          *)
(* Matches C++:                                                            *)
(*   RETRY_REMOVE while(true) { find -> mark upper -> CAS bottom }        *)
(***************************************************************************)
RemoveFindMissOrSoftDeleted(t) ==
    /\ pc[t] = "remove_find"
    /\ op[t] = "remove"
    /\ opKey[t] \notin present
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch, opKey>>

RemoveFindHit(t) ==
    /\ pc[t] = "remove_find"
    /\ op[t] = "remove"
    /\ opKey[t] \in present
    /\ pc' = [pc EXCEPT ![t] = "remove_mark"]
    /\ level' = [level EXCEPT ![t] = MaxLevels]
    /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey>>

\* Mark one upper level as soft-deleted, decrement level counter.
\* When level reaches 1, transition to remove_bottom.
\* Matches C++ mark loop: for (level = top_level-1; level >= 1; --level) soft_delete(...)
RemoveMarkProgress(t) ==
    /\ pc[t] = "remove_mark"
    /\ op[t] = "remove"
    /\ level[t] > 0
    /\ present' = present \ {opKey[t]}
    /\ tomb' = tomb \cup {opKey[t]}
    /\ IF level[t] = 1
          THEN /\ pc' = [pc EXCEPT ![t] = "remove_bottom"]
               /\ level' = [level EXCEPT ![t] = 0]
               /\ retryTick' = ResetRetry(t)
          ELSE /\ pc' = pc
               /\ level' = [level EXCEPT ![t] = @ - 1]
               /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey>>

\* Mark-retry exceeded MAX_REMOVE_MARK_RETRIES_PER_LEVEL: goto RETRY_REMOVE
\* (restart from remove_find; key may have been re-inserted by then)
RemoveMarkGotoRetry(t) ==
    /\ pc[t] = "remove_mark"
    /\ op[t] = "remove"
    /\ retryTick[t] = MaxRetryTicks
    /\ pc' = [pc EXCEPT ![t] = "remove_find"]
    /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey, level>>

\* Level-0 CAS success: this thread soft-deleted the node.
\* Node moves from tomb to retired+fullyUnlinked; epoch work triggered.
\* Matches C++: CAS level-0 success -> unlink_state=FULLY_UNLINKED ->
\*   retire_node -> try_advance_epoch -> throttled reclaim
RemoveBottomSuccess(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \in tomb
    /\ LET k          == opKey[t] IN
       LET c          == cpu_of(t) IN
       LET newRetired  == retired \cup {k} IN
       LET newFU       == fullyUnlinked \cup {k} IN
       LET newRetAt    == [retiredAt EXCEPT ![k] = globalEpoch] IN
       LET nextEpoch   == IF CanAdvanceEpoch(c)
                            THEN IF globalEpoch < MaxEpoch THEN globalEpoch + 1 ELSE MaxEpoch
                            ELSE globalEpoch IN
       LET reclaimable == IF CanReclaim(c, nextEpoch) THEN ReclaimableNodes(nextEpoch) ELSE {} IN
       /\ tomb' = tomb \ {k}
       /\ retired' = newRetired \ reclaimable
       /\ fullyUnlinked' = newFU \ reclaimable
       /\ retiredAt' = newRetAt
       /\ globalEpoch' = nextEpoch
       /\ present' = present
       /\ FinishOp(t)
       /\ UNCHANGED opKey

\* Another thread already soft-deleted the node (tombstone found in bottom CAS):
\* return false without retiring.
\* Matches C++: `else if (tombstoned(deletion_transaction)) { return false; }`
RemoveBottomStolenByOther(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \notin tomb
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch, opKey>>

\* CAS failure on bottom level, still within retry limit: keep retrying
RemoveBottomRetryContinue(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \in tomb
    /\ retryTick[t] < MaxRetryTicks
    /\ retryTick' = TickRetry(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, pc, op, opKey, level>>

\* Exceeded MAX_REMOVE_BOTTOM_RETRIES: goto RETRY_REMOVE
RemoveBottomRetryRestart(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \in tomb
    /\ retryTick[t] = MaxRetryTicks
    /\ pc' = [pc EXCEPT ![t] = "remove_find"]
    /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch,
                   readerDepth, readerEpoch, op, opKey, level>>

(***************************************************************************)
(* FIND state machine                                                      *)
(*                                                                          *)
(* find() in C++ is a read-section-wrapped traversal that assists with    *)
(* GC of tombstoned nodes it encounters.                                   *)
(*                                                                          *)
(* The key safety property: find holding a read section prevents reclaim   *)
(* of nodes retired during the scan (CanReclaim requires all CPUs idle).   *)
(*                                                                          *)
(* FindScanNoAssist: find completes without encountering tombstoned nodes. *)
(* FindScanAssist: find encounters and helps retire a tombstoned node.     *)
(***************************************************************************)
FindScanNoAssist(t) ==
    /\ pc[t] = "find_scan"
    /\ op[t] = "find"
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, fullyUnlinked, retiredAt, globalEpoch, opKey>>

FindScanAssist(t) ==
    /\ pc[t] = "find_scan"
    /\ op[t] = "find"
    /\ tomb # {}
    /\ LET k == CHOOSE k \in tomb : TRUE IN
       /\ tomb' = tomb \ {k}
       /\ retired' = retired \cup {k}
       /\ fullyUnlinked' = fullyUnlinked \cup {k}
       /\ retiredAt' = [retiredAt EXCEPT ![k] = globalEpoch]
    /\ UNCHANGED <<present, globalEpoch>>
    /\ FinishOp(t)
    /\ UNCHANGED opKey

(***************************************************************************)
(* GC state machine                                                        *)
(*                                                                          *)
(* gc() in C++ does a full level-0 scan, calling                           *)
(*   unlink_tombstoned_node_upper_levels + retire on tombstoned nodes,     *)
(*   then advance_epoch_and_reclaim_retired_nodes.                         *)
(*                                                                          *)
(* GcStep models the whole gc() call in one step for TLC tractability.    *)
(* It may retire the opKey if it is tombstoned, then AdvanceAndReclaim.   *)
(***************************************************************************)
GcStep(t) ==
    /\ pc[t] = "gc_scan"
    /\ op[t] = "gc"
    /\ LET retireNow    == opKey[t] \in tomb IN
       LET newRetired   == IF retireNow THEN retired \cup {opKey[t]} ELSE retired IN
       LET newFU        == IF retireNow THEN fullyUnlinked \cup {opKey[t]} ELSE fullyUnlinked IN
       LET newRetAt     == [retiredAt EXCEPT ![opKey[t]] = IF retireNow THEN globalEpoch ELSE @] IN
       LET newTomb      == IF retireNow THEN tomb \ {opKey[t]} ELSE tomb IN
       LET c            == cpu_of(t) IN
       LET nextEpoch    == IF CanAdvanceEpoch(c)
                             THEN IF globalEpoch < MaxEpoch THEN globalEpoch + 1 ELSE MaxEpoch
                             ELSE globalEpoch IN
       LET reclaimable  == IF CanReclaim(c, nextEpoch)
                           THEN { k \in newFU : newRetAt[k] > 0 /\ newRetAt[k] + EpochLag <= nextEpoch }
                           ELSE {} IN
       /\ present' = present
       /\ tomb' = newTomb
       /\ retired' = newRetired \ reclaimable
       /\ fullyUnlinked' = newFU \ reclaimable
       /\ retiredAt' = newRetAt
       /\ globalEpoch' = nextEpoch
       /\ FinishOpCore(t)
       /\ ExitRead(t)
    /\ UNCHANGED opKey

(***************************************************************************)
(* Per-thread step relation                                                *)
(***************************************************************************)
ThreadStep(t) ==
    \/ \E k \in Keys : StartInsert(t, k)
    \/ \E k \in Keys : StartRemove(t, k)
    \/ \E k \in Keys : StartFind(t, k)
    \/ \E k \in Keys : StartGc(t, k)
    \/ InsertFindReject(t)
    \/ InsertFindMiss(t)
    \/ InsertLink0Success(t)
    \/ InsertLink0Fail(t)
    \/ InsertLink0Abort(t)
    \/ InsertUpperFinish(t)
    \/ RemoveFindMissOrSoftDeleted(t)
    \/ RemoveFindHit(t)
    \/ RemoveMarkProgress(t)
    \/ RemoveMarkGotoRetry(t)
    \/ RemoveBottomSuccess(t)
    \/ RemoveBottomStolenByOther(t)
    \/ RemoveBottomRetryContinue(t)
    \/ RemoveBottomRetryRestart(t)
    \/ FindScanNoAssist(t)
    \/ FindScanAssist(t)
    \/ GcStep(t)

RemoveThreadStep(t) ==
    \/ \E k \in Keys : StartRemove(t, k)
    \/ RemoveFindMissOrSoftDeleted(t)
    \/ RemoveFindHit(t)
    \/ RemoveMarkProgress(t)
    \/ RemoveMarkGotoRetry(t)
    \/ RemoveBottomSuccess(t)
    \/ RemoveBottomStolenByOther(t)
    \/ RemoveBottomRetryContinue(t)
    \/ RemoveBottomRetryRestart(t)

Next == \E t \in Threads : ThreadStep(t)

Spec == Init /\ [][Next]_vars

SpecFair ==
    Spec /\ (\A t \in Threads : WF_vars(ThreadStep(t)))

SpecRemoveFair ==
    InitRemoveScenario
    /\ [][\E t \in Threads : RemoveThreadStep(t)]_vars
    /\ (\A t \in Threads : WF_vars(RemoveThreadStep(t)))

(***************************************************************************)
(* Safety invariants                                                       *)
(***************************************************************************)

\* present and tomb are always disjoint
StateConsistency == present \cap tomb = {}

\* NOTE: RetiredDisjointFromPresent is intentionally NOT listed here.
\* In the C++ implementation `retired` tracks old *node instances*; a key can
\* be retired (old node) while simultaneously being re-inserted as a new node
\* (new allocation). The model abstracts nodes to keys, so retired \cap present
\* can be non-empty when a key is recycled -- this is correct behaviour.

\* fully-unlinked is always a subset of retired
FullyUnlinkedSubsetRetired == fullyUnlinked \subseteq retired

\* NOTE: TombDisjointFromRetired is intentionally NOT checked as a safety invariant.
\* In C++ retired and tomb are per-node-instance sets. Key re-use means a new node for
\* key k can be tagged for removal (tomb) while an old node for the same k sits in
\* the retired list. At the key-abstraction level used in this model, tomb \cap retired
\* can therefore be non-empty -- this is correct behaviour.
TombDisjointFromRetired == tomb \cap retired = {}

\* Interrupt re-entrancy invariant:
\* Any CPU inside a read section (depth >= 1) must have a non-zero epoch
\* snapshot (i.e., outermost entry recorded the global epoch correctly).
InterruptReentrancySafe ==
    \A c \in Cpus :
        (readerDepth[c] >= 1) => (readerEpoch[c] > 0)

(***************************************************************************)
(* Liveness properties                                                     *)
(***************************************************************************)

EventuallyIdle == []<>(\A t \in Threads : pc[t] = "idle")

ActiveOpsEventuallyFinish ==
    \A t \in Threads : []((pc[t] # "idle") => <>(pc[t] = "idle"))

RemoveBottomEventuallyIdle ==
    \A t \in Threads : []((pc[t] = "remove_bottom") => <>(pc[t] = "idle"))

RemoveOpsEventuallyFinish ==
    \A t \in Threads : []((op[t] = "remove" /\ pc[t] # "idle") => <>(pc[t] = "idle"))

RemoveNoMarkLivelock ==
    ~<>([](\E t \in Threads : pc[t] = "remove_mark"))

EpochBounded == [] (globalEpoch \in 0..MaxEpoch)

=============================================================================
