---- MODULE LockfreeSkiplist_TTrace_1771271277 ----
EXTENDS Sequences, TLCExt, Toolbox, Naturals, TLC, LockfreeSkiplist

_expression ==
    LET LockfreeSkiplist_TEExpression == INSTANCE LockfreeSkiplist_TEExpression
    IN LockfreeSkiplist_TEExpression!expression
----

_trace ==
    LET LockfreeSkiplist_TETrace == INSTANCE LockfreeSkiplist_TETrace
    IN LockfreeSkiplist_TETrace!trace
----

_inv ==
    ~(
        TLCGet("level") = Len(_TETrace)
        /\
        readerDepth = (<<1>>)
        /\
        globalEpoch = (2)
        /\
        op = (<<"insert">>)
        /\
        level = (<<1>>)
        /\
        tomb = ({})
        /\
        opKey = (<<1>>)
        /\
        pc = (<<"insert_link_upper">>)
        /\
        readerEpoch = (<<2>>)
        /\
        retryTick = (<<0>>)
        /\
        retiredAt = (<<1, 0>>)
        /\
        retired = ({1})
        /\
        present = ({1})
        /\
        readerActive = (<<TRUE>>)
    )
----

_init ==
    /\ globalEpoch = _TETrace[1].globalEpoch
    /\ level = _TETrace[1].level
    /\ readerActive = _TETrace[1].readerActive
    /\ op = _TETrace[1].op
    /\ present = _TETrace[1].present
    /\ pc = _TETrace[1].pc
    /\ readerDepth = _TETrace[1].readerDepth
    /\ retryTick = _TETrace[1].retryTick
    /\ readerEpoch = _TETrace[1].readerEpoch
    /\ tomb = _TETrace[1].tomb
    /\ opKey = _TETrace[1].opKey
    /\ retiredAt = _TETrace[1].retiredAt
    /\ retired = _TETrace[1].retired
----

_next ==
    /\ \E i,j \in DOMAIN _TETrace:
        /\ \/ /\ j = i + 1
              /\ i = TLCGet("level")
        /\ globalEpoch  = _TETrace[i].globalEpoch
        /\ globalEpoch' = _TETrace[j].globalEpoch
        /\ level  = _TETrace[i].level
        /\ level' = _TETrace[j].level
        /\ readerActive  = _TETrace[i].readerActive
        /\ readerActive' = _TETrace[j].readerActive
        /\ op  = _TETrace[i].op
        /\ op' = _TETrace[j].op
        /\ present  = _TETrace[i].present
        /\ present' = _TETrace[j].present
        /\ pc  = _TETrace[i].pc
        /\ pc' = _TETrace[j].pc
        /\ readerDepth  = _TETrace[i].readerDepth
        /\ readerDepth' = _TETrace[j].readerDepth
        /\ retryTick  = _TETrace[i].retryTick
        /\ retryTick' = _TETrace[j].retryTick
        /\ readerEpoch  = _TETrace[i].readerEpoch
        /\ readerEpoch' = _TETrace[j].readerEpoch
        /\ tomb  = _TETrace[i].tomb
        /\ tomb' = _TETrace[j].tomb
        /\ opKey  = _TETrace[i].opKey
        /\ opKey' = _TETrace[j].opKey
        /\ retiredAt  = _TETrace[i].retiredAt
        /\ retiredAt' = _TETrace[j].retiredAt
        /\ retired  = _TETrace[i].retired
        /\ retired' = _TETrace[j].retired

\* Uncomment the ASSUME below to write the states of the error trace
\* to the given file in Json format. Note that you can pass any tuple
\* to `JsonSerialize`. For example, a sub-sequence of _TETrace.
    \* ASSUME
    \*     LET J == INSTANCE Json
    \*         IN J!JsonSerialize("LockfreeSkiplist_TTrace_1771271277.json", _TETrace)

=============================================================================

 Note that you can extract this module `LockfreeSkiplist_TEExpression`
  to a dedicated file to reuse `expression` (the module in the 
  dedicated `LockfreeSkiplist_TEExpression.tla` file takes precedence 
  over the module `LockfreeSkiplist_TEExpression` below).

---- MODULE LockfreeSkiplist_TEExpression ----
EXTENDS Sequences, TLCExt, Toolbox, Naturals, TLC, LockfreeSkiplist

expression == 
    [
        \* To hide variables of the `LockfreeSkiplist` spec from the error trace,
        \* remove the variables below.  The trace will be written in the order
        \* of the fields of this record.
        globalEpoch |-> globalEpoch
        ,level |-> level
        ,readerActive |-> readerActive
        ,op |-> op
        ,present |-> present
        ,pc |-> pc
        ,readerDepth |-> readerDepth
        ,retryTick |-> retryTick
        ,readerEpoch |-> readerEpoch
        ,tomb |-> tomb
        ,opKey |-> opKey
        ,retiredAt |-> retiredAt
        ,retired |-> retired
        
        \* Put additional constant-, state-, and action-level expressions here:
        \* ,_stateNumber |-> _TEPosition
        \* ,_globalEpochUnchanged |-> globalEpoch = globalEpoch'
        
        \* Format the `globalEpoch` variable as Json value.
        \* ,_globalEpochJson |->
        \*     LET J == INSTANCE Json
        \*     IN J!ToJson(globalEpoch)
        
        \* Lastly, you may build expressions over arbitrary sets of states by
        \* leveraging the _TETrace operator.  For example, this is how to
        \* count the number of times a spec variable changed up to the current
        \* state in the trace.
        \* ,_globalEpochModCount |->
        \*     LET F[s \in DOMAIN _TETrace] ==
        \*         IF s = 1 THEN 0
        \*         ELSE IF _TETrace[s].globalEpoch # _TETrace[s-1].globalEpoch
        \*             THEN 1 + F[s-1] ELSE F[s-1]
        \*     IN F[_TEPosition - 1]
    ]

=============================================================================



Parsing and semantic processing can take forever if the trace below is long.
 In this case, it is advised to uncomment the module below to deserialize the
 trace from a generated binary file.

\*
\*---- MODULE LockfreeSkiplist_TETrace ----
\*EXTENDS IOUtils, TLC, LockfreeSkiplist
\*
\*trace == IODeserialize("LockfreeSkiplist_TTrace_1771271277.bin", TRUE)
\*
\*=============================================================================
\*

---- MODULE LockfreeSkiplist_TETrace ----
EXTENDS TLC, LockfreeSkiplist

trace == 
    <<
    ([readerDepth |-> <<0>>,globalEpoch |-> 1,op |-> <<"none">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"idle">>,readerEpoch |-> <<0>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<FALSE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"insert">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"insert_find">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"insert">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"insert_link0">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"insert">>,level |-> <<1>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"insert_link_upper">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {1},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<0>>,globalEpoch |-> 1,op |-> <<"none">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"idle">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {1},readerActive |-> <<FALSE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"remove">>,level |-> <<2>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"remove_find">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {1},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"remove">>,level |-> <<2>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"remove_mark">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {1},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"remove">>,level |-> <<1>>,tomb |-> {1},opKey |-> <<1>>,pc |-> <<"remove_mark">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"remove">>,level |-> <<0>>,tomb |-> {1},opKey |-> <<1>>,pc |-> <<"remove_bottom">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<0>>,globalEpoch |-> 1,op |-> <<"none">>,level |-> <<0>>,tomb |-> {1},opKey |-> <<1>>,pc |-> <<"idle">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<FALSE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 1,op |-> <<"gc">>,level |-> <<2>>,tomb |-> {1},opKey |-> <<1>>,pc |-> <<"gc_scan">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<0, 0>>,retired |-> {},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<0>>,globalEpoch |-> 2,op |-> <<"none">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"idle">>,readerEpoch |-> <<1>>,retryTick |-> <<0>>,retiredAt |-> <<1, 0>>,retired |-> {1},present |-> {},readerActive |-> <<FALSE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 2,op |-> <<"insert">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"insert_find">>,readerEpoch |-> <<2>>,retryTick |-> <<0>>,retiredAt |-> <<1, 0>>,retired |-> {1},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 2,op |-> <<"insert">>,level |-> <<0>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"insert_link0">>,readerEpoch |-> <<2>>,retryTick |-> <<0>>,retiredAt |-> <<1, 0>>,retired |-> {1},present |-> {},readerActive |-> <<TRUE>>]),
    ([readerDepth |-> <<1>>,globalEpoch |-> 2,op |-> <<"insert">>,level |-> <<1>>,tomb |-> {},opKey |-> <<1>>,pc |-> <<"insert_link_upper">>,readerEpoch |-> <<2>>,retryTick |-> <<0>>,retiredAt |-> <<1, 0>>,retired |-> {1},present |-> {1},readerActive |-> <<TRUE>>])
    >>
----


=============================================================================

---- CONFIG LockfreeSkiplist_TTrace_1771271277 ----
CONSTANTS
    Keys = { 1 , 2 }
    Threads = { 1 }
    Cpus = { 1 }
    MaxLevels = 2
    MaxRetryTicks = 3
    MaxEpoch = 8
    EpochLag = 2

INVARIANT
    _inv

CHECK_DEADLOCK
    \* CHECK_DEADLOCK off because of PROPERTY or INVARIANT above.
    FALSE

INIT
    _init

NEXT
    _next

CONSTANT
    _TETrace <- _trace

ALIAS
    _expression
=============================================================================
\* Generated on Mon Feb 16 12:47:58 MST 2026