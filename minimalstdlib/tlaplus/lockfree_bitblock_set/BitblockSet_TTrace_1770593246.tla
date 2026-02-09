---- MODULE BitblockSet_TTrace_1770593246 ----
EXTENDS Sequences, TLCExt, Toolbox, BitblockSet, Naturals, TLC

_expression ==
    LET BitblockSet_TEExpression == INSTANCE BitblockSet_TEExpression
    IN BitblockSet_TEExpression!expression
----

_trace ==
    LET BitblockSet_TETrace == INSTANCE BitblockSet_TETrace
    IN BitblockSet_TETrace!trace
----

_inv ==
    ~(
        TLCGet("level") = Len(_TETrace)
        /\
        lM = ((<<1, 0>> :> {} @@ <<2, 0>> :> {}))
        /\
        sW = ((<<1, 0>> :> 0 @@ <<2, 0>> :> 0))
        /\
        fM = ((<<1, 0>> :> {0} @@ <<2, 0>> :> {0}))
        /\
        words = ((0 :> {0, 1} @@ 1 :> {}))
        /\
        tentative = ((<<1, 0>> :> {} @@ <<2, 0>> :> {}))
        /\
        sb = ((<<1, 0>> :> 0 @@ <<2, 0>> :> 0))
        /\
        firstGlob = ((<<1, 0>> :> {0} @@ <<2, 0>> :> {0}))
        /\
        activeDepth = (<<0, 0>>)
        /\
        eW = ((<<1, 0>> :> 0 @@ <<2, 0>> :> 0))
        /\
        pc = ((<<1, 0>> :> "idle" @@ <<2, 0>> :> "idle"))
        /\
        lastGlob = ((<<1, 0>> :> {} @@ <<2, 0>> :> {}))
        /\
        ops = ((<<1, 0>> :> 2 @@ <<2, 0>> :> 2))
        /\
        nb = ((<<1, 0>> :> 1 @@ <<2, 0>> :> 1))
        /\
        owned = ((<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}))
        /\
        savedOld = ((<<1, 0>> :> {} @@ <<2, 0>> :> {}))
        /\
        acqB = ((<<1, 0>> :> {0} @@ <<2, 0>> :> {0}))
    )
----

_init ==
    /\ nb = _TETrace[1].nb
    /\ savedOld = _TETrace[1].savedOld
    /\ pc = _TETrace[1].pc
    /\ eW = _TETrace[1].eW
    /\ fM = _TETrace[1].fM
    /\ sW = _TETrace[1].sW
    /\ sb = _TETrace[1].sb
    /\ lastGlob = _TETrace[1].lastGlob
    /\ acqB = _TETrace[1].acqB
    /\ firstGlob = _TETrace[1].firstGlob
    /\ ops = _TETrace[1].ops
    /\ activeDepth = _TETrace[1].activeDepth
    /\ tentative = _TETrace[1].tentative
    /\ words = _TETrace[1].words
    /\ lM = _TETrace[1].lM
    /\ owned = _TETrace[1].owned
----

_next ==
    /\ \E i,j \in DOMAIN _TETrace:
        /\ \/ /\ j = i + 1
              /\ i = TLCGet("level")
        /\ nb  = _TETrace[i].nb
        /\ nb' = _TETrace[j].nb
        /\ savedOld  = _TETrace[i].savedOld
        /\ savedOld' = _TETrace[j].savedOld
        /\ pc  = _TETrace[i].pc
        /\ pc' = _TETrace[j].pc
        /\ eW  = _TETrace[i].eW
        /\ eW' = _TETrace[j].eW
        /\ fM  = _TETrace[i].fM
        /\ fM' = _TETrace[j].fM
        /\ sW  = _TETrace[i].sW
        /\ sW' = _TETrace[j].sW
        /\ sb  = _TETrace[i].sb
        /\ sb' = _TETrace[j].sb
        /\ lastGlob  = _TETrace[i].lastGlob
        /\ lastGlob' = _TETrace[j].lastGlob
        /\ acqB  = _TETrace[i].acqB
        /\ acqB' = _TETrace[j].acqB
        /\ firstGlob  = _TETrace[i].firstGlob
        /\ firstGlob' = _TETrace[j].firstGlob
        /\ ops  = _TETrace[i].ops
        /\ ops' = _TETrace[j].ops
        /\ activeDepth  = _TETrace[i].activeDepth
        /\ activeDepth' = _TETrace[j].activeDepth
        /\ tentative  = _TETrace[i].tentative
        /\ tentative' = _TETrace[j].tentative
        /\ words  = _TETrace[i].words
        /\ words' = _TETrace[j].words
        /\ lM  = _TETrace[i].lM
        /\ lM' = _TETrace[j].lM
        /\ owned  = _TETrace[i].owned
        /\ owned' = _TETrace[j].owned

\* Uncomment the ASSUME below to write the states of the error trace
\* to the given file in Json format. Note that you can pass any tuple
\* to `JsonSerialize`. For example, a sub-sequence of _TETrace.
    \* ASSUME
    \*     LET J == INSTANCE Json
    \*         IN J!JsonSerialize("BitblockSet_TTrace_1770593246.json", _TETrace)

=============================================================================

 Note that you can extract this module `BitblockSet_TEExpression`
  to a dedicated file to reuse `expression` (the module in the 
  dedicated `BitblockSet_TEExpression.tla` file takes precedence 
  over the module `BitblockSet_TEExpression` below).

---- MODULE BitblockSet_TEExpression ----
EXTENDS Sequences, TLCExt, Toolbox, BitblockSet, Naturals, TLC

expression == 
    [
        \* To hide variables of the `BitblockSet` spec from the error trace,
        \* remove the variables below.  The trace will be written in the order
        \* of the fields of this record.
        nb |-> nb
        ,savedOld |-> savedOld
        ,pc |-> pc
        ,eW |-> eW
        ,fM |-> fM
        ,sW |-> sW
        ,sb |-> sb
        ,lastGlob |-> lastGlob
        ,acqB |-> acqB
        ,firstGlob |-> firstGlob
        ,ops |-> ops
        ,activeDepth |-> activeDepth
        ,tentative |-> tentative
        ,words |-> words
        ,lM |-> lM
        ,owned |-> owned
        
        \* Put additional constant-, state-, and action-level expressions here:
        \* ,_stateNumber |-> _TEPosition
        \* ,_nbUnchanged |-> nb = nb'
        
        \* Format the `nb` variable as Json value.
        \* ,_nbJson |->
        \*     LET J == INSTANCE Json
        \*     IN J!ToJson(nb)
        
        \* Lastly, you may build expressions over arbitrary sets of states by
        \* leveraging the _TETrace operator.  For example, this is how to
        \* count the number of times a spec variable changed up to the current
        \* state in the trace.
        \* ,_nbModCount |->
        \*     LET F[s \in DOMAIN _TETrace] ==
        \*         IF s = 1 THEN 0
        \*         ELSE IF _TETrace[s].nb # _TETrace[s-1].nb
        \*             THEN 1 + F[s-1] ELSE F[s-1]
        \*     IN F[_TEPosition - 1]
    ]

=============================================================================



Parsing and semantic processing can take forever if the trace below is long.
 In this case, it is advised to uncomment the module below to deserialize the
 trace from a generated binary file.

\*
\*---- MODULE BitblockSet_TETrace ----
\*EXTENDS IOUtils, BitblockSet, TLC
\*
\*trace == IODeserialize("BitblockSet_TTrace_1770593246.bin", TRUE)
\*
\*=============================================================================
\*

---- MODULE BitblockSet_TETrace ----
EXTENDS BitblockSet, TLC

trace == 
    <<
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),words |-> (0 :> {} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),words |-> (0 :> {} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "acq_sw" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),words |-> (0 :> {1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "done" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),words |-> (0 :> {1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {}),words |-> (0 :> {1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "acq_sw" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "done" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 0),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "acq_sw"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 1),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "done"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 0),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 1),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 1),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 1),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "acq_sw"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 1),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 1),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "done"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 1),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 1),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0})]),
    ([lM |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),fM |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),words |-> (0 :> {0, 1} @@ 1 :> {}),tentative |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),sb |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),firstGlob |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0}),activeDepth |-> <<0, 0>>,eW |-> (<<1, 0>> :> 0 @@ <<2, 0>> :> 0),pc |-> (<<1, 0>> :> "idle" @@ <<2, 0>> :> "idle"),lastGlob |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),ops |-> (<<1, 0>> :> 2 @@ <<2, 0>> :> 2),nb |-> (<<1, 0>> :> 1 @@ <<2, 0>> :> 1),owned |-> (<<1, 0>> :> {0, 1} @@ <<2, 0>> :> {}),savedOld |-> (<<1, 0>> :> {} @@ <<2, 0>> :> {}),acqB |-> (<<1, 0>> :> {0} @@ <<2, 0>> :> {0})])
    >>
----


=============================================================================

---- CONFIG BitblockSet_TTrace_1770593246 ----
CONSTANTS
    BPW = 2
    NW = 2
    NumCPUs = 2
    MaxInterruptDepth = 0
    MaxBits = 2
    MaxOps = 2

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
\* Generated on Sun Feb 08 16:27:28 MST 2026