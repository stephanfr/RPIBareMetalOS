-------------------------- MODULE LockfreeSkiplist --------------------------
(***************************************************************************)
(* Bounded model for current include/lockfree/skiplist with per-CPU epoch  *)
(* read sections and lagged retired-node reclamation.                       *)
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
    present,
    tomb,
    retired,
    retiredAt,
    globalEpoch,
    readerDepth,
    readerActive,
    readerEpoch,
    pc,
    op,
    opKey,
    level,
    retryTick

vars == <<present, tomb, retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, pc, op, opKey, level, retryTick>>

PcStates == {
    "idle",
    "insert_find", "insert_link0", "insert_link_upper",
    "remove_find", "remove_mark", "remove_bottom",
    "gc_scan"
}

Ops == {"none", "insert", "remove", "gc"}

cpu_of(t) == IF t \in Cpus THEN t ELSE CHOOSE c \in Cpus : TRUE

TypeOK ==
    /\ present \subseteq Keys
    /\ tomb \subseteq Keys
    /\ retired \subseteq Keys
    /\ present \cap tomb = {}
    /\ retiredAt \in [Keys -> 0..MaxEpoch]
    /\ globalEpoch \in 0..MaxEpoch
    /\ readerDepth \in [Cpus -> 0..MaxReadDepth]
    /\ readerActive \in [Cpus -> BOOLEAN]
    /\ readerEpoch \in [Cpus -> 0..MaxEpoch]
    /\ pc \in [Threads -> PcStates]
    /\ op \in [Threads -> Ops]
    /\ opKey \in [Threads -> Keys]
    /\ level \in [Threads -> 0..MaxLevels]
    /\ retryTick \in [Threads -> 0..MaxRetryTicks]
    /\ Threads # {}
    /\ Cpus # {}
    /\ \A c \in Cpus : readerActive[c] => readerDepth[c] > 0

Init ==
    /\ present = {}
    /\ tomb = {}
    /\ retired = {}
    /\ retiredAt = [k \in Keys |-> 0]
    /\ globalEpoch = 1
    /\ readerDepth = [c \in Cpus |-> 0]
    /\ readerActive = [c \in Cpus |-> FALSE]
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
    /\ retiredAt = [k \in Keys |-> 0]
    /\ globalEpoch = 1
    /\ readerDepth = [c \in Cpus |-> 0]
    /\ readerActive = [c \in Cpus |-> FALSE]
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

EnterRead(t) ==
    LET c == cpu_of(t) IN
    /\ readerDepth[c] < MaxReadDepth
    /\ readerDepth' = [readerDepth EXCEPT ![c] = @ + 1]
    /\ readerActive' = [readerActive EXCEPT ![c] = TRUE]
    /\ readerEpoch' = [readerEpoch EXCEPT ![c] = IF readerDepth[c] = 0 THEN globalEpoch ELSE @]

ExitRead(t) ==
    LET c == cpu_of(t) IN
    /\ readerDepth[c] > 0
    /\ readerDepth' = [readerDepth EXCEPT ![c] = @ - 1]
    /\ readerActive' = [readerActive EXCEPT ![c] = (readerDepth[c] > 1)]
    /\ readerEpoch' = readerEpoch

FinishOpCore(t) ==
    /\ pc' = [pc EXCEPT ![t] = "idle"]
    /\ op' = [op EXCEPT ![t] = "none"]
    /\ level' = [level EXCEPT ![t] = 0]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]

FinishOp(t) ==
    /\ FinishOpCore(t)
    /\ ExitRead(t)

StartInsert(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "insert_find"]
    /\ op' = [op EXCEPT ![t] = "insert"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = 0]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch>>

StartRemove(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "remove_find"]
    /\ op' = [op EXCEPT ![t] = "remove"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = MaxLevels]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch>>

StartGc(t, k) ==
    /\ pc[t] = "idle"
    /\ EnterRead(t)
    /\ pc' = [pc EXCEPT ![t] = "gc_scan"]
    /\ op' = [op EXCEPT ![t] = "gc"]
    /\ opKey' = [opKey EXCEPT ![t] = k]
    /\ level' = [level EXCEPT ![t] = MaxLevels]
    /\ retryTick' = [retryTick EXCEPT ![t] = 0]
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch>>

InsertFindReject(t) ==
    /\ pc[t] = "insert_find"
    /\ op[t] = "insert"
    /\ opKey[t] \in (present \cup tomb)
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, opKey>>

InsertFindMiss(t) ==
    /\ pc[t] = "insert_find"
    /\ op[t] = "insert"
    /\ opKey[t] \notin (present \cup tomb)
    /\ pc' = [pc EXCEPT ![t] = "insert_link0"]
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, op, opKey, level, retryTick>>

InsertLink0Success(t) ==
    /\ pc[t] = "insert_link0"
    /\ op[t] = "insert"
    /\ opKey[t] \notin (present \cup tomb)
    /\ present' = present \cup {opKey[t]}
    /\ tomb' = tomb
    /\ pc' = [pc EXCEPT ![t] = "insert_link_upper"]
    /\ op' = op
    /\ level' = [level EXCEPT ![t] = 1]
    /\ retryTick' = retryTick
    /\ UNCHANGED <<retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, opKey>>

InsertLink0Fail(t) ==
    /\ pc[t] = "insert_link0"
    /\ op[t] = "insert"
    /\ retryTick[t] < MaxRetryTicks
    /\ pc' = [pc EXCEPT ![t] = "insert_find"]
    /\ retryTick' = TickRetry(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, op, opKey, level>>

InsertLink0Abort(t) ==
    /\ pc[t] = "insert_link0"
    /\ op[t] = "insert"
    /\ retryTick[t] = MaxRetryTicks
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, opKey>>

InsertUpperFinish(t) ==
    /\ pc[t] = "insert_link_upper"
    /\ op[t] = "insert"
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, opKey>>

RemoveFindMissOrSoftDeleted(t) ==
    /\ pc[t] = "remove_find"
    /\ op[t] = "remove"
    /\ opKey[t] \notin present
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, opKey>>

RemoveFindHit(t) ==
    /\ pc[t] = "remove_find"
    /\ op[t] = "remove"
    /\ opKey[t] \in present
    /\ pc' = [pc EXCEPT ![t] = "remove_mark"]
    /\ op' = op
    /\ level' = [level EXCEPT ![t] = MaxLevels]
    /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, opKey>>

RemoveMarkProgress(t) ==
    /\ pc[t] = "remove_mark"
    /\ op[t] = "remove"
    /\ level[t] > 0
    /\ present' = present \ {opKey[t]}
    /\ tomb' = tomb \cup {opKey[t]}
    /\ IF level[t] = 1
          THEN /\ pc' = [pc EXCEPT ![t] = "remove_bottom"]
               /\ op' = op
               /\ level' = [level EXCEPT ![t] = 0]
               /\ retryTick' = ResetRetry(t)
          ELSE /\ pc' = pc
               /\ op' = op
               /\ level' = [level EXCEPT ![t] = @ - 1]
               /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, opKey>>

RemoveBottomSuccess(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \in tomb
    /\ FinishOp(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, opKey>>

RemoveBottomRetryContinue(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \notin tomb
    /\ retryTick[t] < MaxRetryTicks
    /\ retryTick' = TickRetry(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, pc, op, opKey, level>>

RemoveBottomRetryRestart(t) ==
    /\ pc[t] = "remove_bottom"
    /\ op[t] = "remove"
    /\ opKey[t] \notin tomb
    /\ retryTick[t] = MaxRetryTicks
    /\ pc' = [pc EXCEPT ![t] = "remove_find"]
    /\ op' = op
    /\ level' = level
    /\ retryTick' = ResetRetry(t)
    /\ UNCHANGED <<present, tomb, retired, retiredAt, globalEpoch, readerDepth, readerActive, readerEpoch, opKey>>

CanAdvanceEpochExempt(t) ==
    LET callerCpu == cpu_of(t) IN
    \A c \in Cpus :
        (c = callerCpu) \/ (~readerActive[c]) \/ (readerEpoch[c] > globalEpoch)

GcStep(t) ==
    /\ pc[t] = "gc_scan"
    /\ op[t] = "gc"
    /\ LET retireNow == opKey[t] \in tomb IN
       LET retiredAfterRetire == IF retireNow THEN retired \cup {opKey[t]} ELSE retired IN
       LET retiredAtAfterRetire == [retiredAt EXCEPT ![opKey[t]] = IF retireNow THEN globalEpoch ELSE @] IN
       LET nextEpoch == IF CanAdvanceEpochExempt(t)
                          THEN IF globalEpoch < MaxEpoch THEN globalEpoch + 1 ELSE MaxEpoch
                          ELSE globalEpoch IN
       LET reclaimable == {k \in retiredAfterRetire : retiredAtAfterRetire[k] + EpochLag <= nextEpoch} IN
       /\ present' = present
       /\ tomb' = IF retireNow THEN tomb \ {opKey[t]} ELSE tomb
       /\ retired' = retiredAfterRetire \ reclaimable
       /\ retiredAt' = retiredAtAfterRetire
       /\ globalEpoch' = nextEpoch
       /\ FinishOpCore(t)
       /\ ExitRead(t)
    /\ UNCHANGED opKey

ThreadStep(t) ==
    \/ \E k \in Keys : StartInsert(t, k)
    \/ \E k \in Keys : StartRemove(t, k)
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
    \/ RemoveBottomSuccess(t)
    \/ RemoveBottomRetryContinue(t)
    \/ RemoveBottomRetryRestart(t)
    \/ GcStep(t)

RemoveThreadStep(t) ==
    \/ \E k \in Keys : StartRemove(t, k)
    \/ RemoveFindMissOrSoftDeleted(t)
    \/ RemoveFindHit(t)
    \/ RemoveMarkProgress(t)
    \/ RemoveBottomSuccess(t)
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

StateConsistency == present \cap tomb = {}

RetiredDisjointFromPresent == retired \cap present = {}

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
