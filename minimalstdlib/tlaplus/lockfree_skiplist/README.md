# Lockfree Skiplist TLA+ Model

This model captures the **current** `include/lockfree/skiplist` control-flow relevant to deadlock/progress/safety analysis:

- insert/remove/find/gc are modeled as retry-loop state machines
- soft-delete (`tomb`), GC physical removal (`retired`), and the `FULLY_UNLINKED` gate (`fullyUnlinked`) are explicit
- CAS contention is abstracted as nondeterministic retry transitions
- retries are bounded by `MaxRetryTicks` for finite-state model checking
- interrupt re-entrancy is modeled via per-CPU `readerDepth` nesting counter

## Fidelity Notes

The model is intentionally a **bounded abstraction**. It matches the C++ control-flow structure and key safety invariants.

Matched exactly at control-flow level:
- unbounded `while(true)` retry structure in `insert` and `remove` (bounded in model by `MaxRetryTicks`)
- `remove` soft-delete then bottom-level retry/exit shape including `goto RETRY_REMOVE` for excessive retries (`RemoveMarkGotoRetry`, `RemoveBottomRetryRestart`)
- `remove_bottom` path where another thread wins the CAS (`RemoveBottomStolenByOther`, matching `return false` on `tombstoned(deletion_transaction)`)
- `gc` full scan + epoch advance + reclaim
- `find` read-section-wrapped traversal with optional GC assistance (`FindScanAssist`, `FindScanNoAssist`)
- `FULLY_UNLINKED` gate: `fullyUnlinked` set tracks which retired nodes have had all upper-level pointer removals completed; reclaim only happens for nodes in `fullyUnlinked`
- Reclaim guard: `CanReclaim(callerCpu, epoch)` self-exempts the calling CPU and requires all other active readers to have `reader_epoch > reclaim_epoch` (matches C++ `reclaim_from_per_cpu_list` which skips `cpu_slot` and checks remaining readers against `reclaim_epoch = epoch - EpochLag`)
- Epoch advance guard: `CanAdvanceEpoch(callerCpu)` self-exempts the calling CPU and requires every other active reader to be at the current epoch (matches C++ `try_advance_epoch(exempt_slot)`)
- Reader-presence: both guards use `readerDepth[c] = 0` to detect idle slots, matching the fixed C++ code which eliminated the separate `reader_active_` flag; `reader_depth_ > 0` is now the sole authoritative indicator
- Interrupt re-entrancy: `EnterRead` only snapshots the epoch on the **first** nesting level (`readerDepth == 0`); nested ISR callers inherit the outer epoch — matching `if (previous_depth == 0) { epoch_.store(...) }`

Abstracted/simplified:
- pointer identity and memory reclamation internals
- per-level predecessor/successor pointer graph structure
- random level distribution (bounded by `MaxLevels`)
- `SkipNode` allocation: the model uses keys as stand-ins for node instances; a key in `retired` can coexist with a fresh node for the same key in `present` or `tomb` (correct, since they are different allocations in C++)

## Files

- `LockfreeSkiplist.tla` — specification
- `LockfreeSkiplist_deadlock.cfg` — deadlock + safety invariants (2 keys, 1 thread, 1 CPU)
- `LockfreeSkiplist_interrupt_reentrancy.cfg` — **kernel/ISR re-entrancy**: 2 threads sharing 1 CPU, MaxReadDepth=3
- `LockfreeSkiplist_liveness.cfg` — fairness + `ActiveOpsEventuallyFinish` property check
- `LockfreeSkiplist_remove_liveness.cfg` — remove-only `RemoveBottomEventuallyIdle` check
- `LockfreeSkiplist_remove_mark_livelock.cfg` — `RemoveNoMarkLivelock` exclusion check

## Run TLC

```bash
cd /home/steve/dev/RPIBareMetalOS/minimalstdlib/tlaplus/lockfree_skiplist

# Deadlock + safety invariants
java -XX:+UseParallelGC -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_deadlock.cfg LockfreeSkiplist.tla

# Interrupt/kernel re-entrancy safety
java -XX:+UseParallelGC -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_interrupt_reentrancy.cfg LockfreeSkiplist.tla

# Liveness
java -XX:+UseParallelGC -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_liveness.cfg LockfreeSkiplist.tla

# Remove-bottom liveness
java -XX:+UseParallelGC -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_remove_liveness.cfg LockfreeSkiplist.tla

# Remove-mark livelock exclusion
java -XX:+UseParallelGC -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_remove_mark_livelock.cfg LockfreeSkiplist.tla
```

## Results (current run)

All configs pass with **no errors**:

| Config | States | Result |
|---|---|---|
| `deadlock` (2 keys, 1T, 1C) | 12,487 distinct | **No deadlock. All invariants hold.** |
| `interrupt_reentrancy` (2 keys, 2T, 1C, depth=3) | 1,011,937 distinct | **`InterruptReentrancySafe` holds across all states.** |
| `liveness` (1 key, 1T, 1C) | 469 distinct | **`ActiveOpsEventuallyFinish` holds under fairness.** |
| `remove_liveness` (1 key, 1T, 1C) | 14 distinct | **`RemoveBottomEventuallyIdle` holds.** |
| `remove_mark_livelock` (1 key, 1T, 1C) | 14 distinct | **`RemoveNoMarkLivelock` holds.** |

### Kernel/ISR re-entrancy summary

The `interrupt_reentrancy` config exercises 2 threads sharing a single CPU with `MaxReadDepth=3`. TLC exhaustively verified (747,285 states) that:

1. A thread already inside a read section (e.g., kernel thread executing `insert`) can have its CPU preempted by an ISR that also calls a skiplist operation.
2. The nested ISR correctly inherits the outermost epoch snapshot without overwriting it.
3. `InterruptReentrancySafe`: every CPU with `readerDepth ≥ 1` holds a non-zero epoch snapshot — i.e., the epoch was correctly captured before any nested entry.
4. `StateConsistency` (`present ∩ tomb = {}`) and `FullyUnlinkedSubsetRetired` hold throughout.

**Conclusion: the skiplist is kernel and interrupt re-entrancy safe under the bounded model.**
