# Lockfree Skiplist TLA+ Model

This model captures the **current** `include/lockfree/skiplist` control-flow relevant to deadlock/progress analysis:

- insert/remove are modeled as retry-loop state machines
- soft-delete (`tomb`) and gc physical removal are explicit
- CAS contention is abstracted as nondeterministic retry transitions
- retries are modeled as non-stuttering steps via `retryTick` so fairness/liveness checks can expose livelock cycles

## Fidelity Notes

The model is intentionally a **bounded abstraction**. It matches the C++ control-flow structure, including retry loops and `gc` restart behavior, but not every low-level implementation detail.

Matched exactly at control-flow level:
- unbounded `while(true)` retry structure in `insert` and `remove`
- `remove` soft-delete then bottom-level retry/exit shape
- `gc` CAS-failure restart (`goto RETRY`) behavior
- soft-deleted keys block insertion until `gc` removes them

Abstracted/simplified:
- pointer identity and memory reclamation internals
- per-level predecessor/successor pointer graph structure
- random level distribution details (bounded by `MaxLevels`)
- retry counters are bounded by `MaxRetryTicks` for finite-state model checking

## Files

- `LockfreeSkiplist.tla` — specification
- `LockfreeSkiplist_deadlock.cfg` — small deadlock-checking configuration
- `LockfreeSkiplist_liveness.cfg` — fairness + progress property check
- `LockfreeSkiplist_remove_liveness.cfg` — remove-only progress check
- `LockfreeSkiplist_remove_stress.cfg` — 2-thread remove liveness stress check
- `LockfreeSkiplist_remove_mark_livelock.cfg` — explicit remove-mark livelock exclusion check

## Run TLC

```bash
cd /home/steve/dev/RPIBareMetalOS/minimalstdlib/tlaplus/lockfree_skiplist
java -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_deadlock.cfg \
  LockfreeSkiplist.tla
```

TLC checks deadlock by default (this config does **not** disable deadlock checking).

Liveness/progress run:

```bash
java -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_liveness.cfg \
  LockfreeSkiplist.tla
```

Remove-focused liveness run:

```bash
java -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_remove_liveness.cfg \
  LockfreeSkiplist.tla
```

Remove stress run:

```bash
java -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_remove_stress.cfg \
  LockfreeSkiplist.tla
```

Explicit remove-mark livelock exclusion run:

```bash
java -Xmx2g -jar ../tla2tools.jar \
  -config LockfreeSkiplist_remove_mark_livelock.cfg \
  LockfreeSkiplist.tla
```

## Result (current run)

- Deadlock config completed with **no deadlock found**.
- Liveness config reports a **counterexample** showing non-termination via retry loop (`insert_link0` <-> `insert_find`), which is consistent with unbounded retry semantics.
- Remove-focused liveness config (`remove_liveness`) completes with **no counterexample** for the specific `remove_bottom` property in the small bounded case.
- Remove-mark livelock exclusion config reports a **counterexample**: an infinite `remove_mark` retry cycle is reachable.
- Remove stress config (2 threads) also reports a **counterexample** showing one thread can remain in `remove_mark` retry while another continues.
