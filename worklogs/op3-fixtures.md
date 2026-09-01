# op3-fixtures and engine-js-op3 -- wave 1 of the Section 16 rework

WHERE THIS FILE BELONGS.  The project keeps worklogs at
sprints/sprint-3/issue-docs/<issue-key>.md, which is in the
freebsd-development repository.  This session is isolated in a
worktree of the harness repository and can read that tree but not
commit to it, so the worklog is written here, versioned, and is owed
a copy at sprints/sprint-3/issue-docs/op3-fixtures.md.  Same reason
the fixtures themselves are authored here rather than in the demo's
examples directory.

Written 2026-08-31, when the work landed.

## What was built

Sixteen worked examples for the settled open problems 3 and 6, and an
independent implementation of the one genuinely new decide phase.

Twelve op3 fixtures and four op6, all under
trees/open-problems/.  Each was reasoned to an expected outcome BY
HAND from theory Section 16 and op6-set-succession.md, and each
carries a WORKED OUTCOME block recording believed moves (or the
reason for abstention), per-pool lost and gained sets, decisions,
conflicts and quarantines, and the edit shape in scoped coordinates.
The hand-working came first in every case; nothing was run before its
answer was written down.

correspond.js implements Section 16's new phase -- the candidate
closure of Proposition 16.4 phase one and the beliefs of phase two --
and devcheck/correspondcheck.sh runs it over the twelve op3 fixtures
against a table transcribed from their comments.  All twelve agree.

## Decisions, and where they diverged from the plan

THE TRACKER NAMES engine.js AND THIS IS NOT IT.  Both issues list
expected changed files in zfs-rebase-theory/decide-demo/, which this
session cannot commit to.  The precedent already existed: emit.js
lives in the harness repo and is loaded by the demo's runner, for the
same reason.  correspond.js follows it exactly, and correspondcheck.js
follows emitcounts.js.  Porting into engine.js proper is mechanical
and belongs to whoever holds that tree.  What is NOT covered by this
substitution: the rest of engine.js -- pairing, the four passes,
quarantine, succession -- is untouched, so set-delta succession has
no executable reference yet.  Only correspondence does.

GOLD POLICY FOR op6 CHANGED MID-WORK.  The four op6 fixtures were
first written with today's engine behaviour as gold and the proposal
recorded in comments, because the proposal was pending.  When the
lead confirmed, the gold became the settled answer.  The consequence
is deliberate and should not be treated as a break:
op6-fragment-multi is EXPECTED TO FAIL against the current engine and
turns green when wave 2 lands set-valued succession.

TWO OF THE FOUR op6 SHAPES CARRY SETTLEMENT EVIDENCE, corrected
after the theory lane challenged the first count.  op6-fragment-multi
carries the VERDICT change, conflict to clean.  op6-wholesale-quiet
carries the REALIZATION change: identical tree shape before and
after, different dnode, because the old degrade found no contributor
and materialized -- which is the silent-materialize defect itself and
so the more important of the two.  op6-singleton-vs-split is a true
control and op6-straddle-split conflicts both ways.

The error is worth recording because of its shape, not its size.  The
tally was derived for an EARLIER set of four that contained a
genuinely inert control, and was carried forward unchanged when the
assignment replaced that control with the wholesale-rename case.  The
fixture's own comment described the materialize correctly the whole
time; the summary contradicted the file it summarised.  A claim about
a set survived the set being swapped.

## Verification, and what was deferred

Both parsers agree on all 53 corpus fixtures, byte for byte
(devcheck/treecheck.sh).  ASCII clean.  The suite's own gates are
green.

Twelve correspondence results agree with the hand-worked
expectations.  The runner was PROVEN TO FAIL FIRST: a planted
expectation was reported as a disagreement with both readings printed
side by side, then reverted.

DEFERRED, and this is the important line.  The gold sections are
CLAIMS.  No engine has run any of these sixteen fixtures, so
survival, pooling, content and realization are all unverified.  What
has been executed is the correspondence phase alone, and only its
believed moves.  Everything else waits on wave 2 and a box.

## Findings, in the order they matter

A WORDING PROBLEM IN THE THEORY, reported to the theory lane.
Definition 16.3 says the sibling generator works under an
already-CORRESPONDED parent; Proposition 16.4 says under pairs
already PRESENT.  Only the second reading makes the proposition's own
argument work -- on the first, belief must precede closure and phase
one is judging, which is the hazard the proposition exists to remove.
It is also the only reading under which phase one is a fixed point at
all, since red, green and red-anchor pairs are computable in a single
pass and the sibling generator is the sole reason there is anything
to iterate.  op3-nested-recreated assumes Proposition 16.4's wording;
if Definition 16.3 means what it says, that fixture's gold is wrong
AND the proposition is unsound.

A BUG IN THIS LANE'S OWN IMPLEMENTATION, found by hand-working
op3-dir-yellow-abstain.  correspond.js denied directory children the
name-match row of the vote triad.  Definition 16.3 restricts exactly
one row to non-directories -- yellow, and it says so, because
Definition 2.8 compares directories by attributes and a
default-attributed directory is yellow to nearly every other one.
The name-match row carries no such restriction and should not: a leaf
is unique inside a directory whatever it names.  All ten fixtures
written before that one agreed with the buggy code, because in every
one of them a file did the voting.  Fixed, and verified by
reintroducing the bug and watching the new fixture report it.

A ONE-DIRECTIONAL DRIFT GATE.  sync-trees.sh walked only the demo's
fixtures, so it reported "byte-identical" while this side held
fourteen the demo had never heard of.  Fixed with a reverse scan that
names every harness-only fixture and never claims identity falsely.
Deliberately not a failure, since nothing on this side can turn it
green.

A STALE PREMISE.  Both sync-trees.sh and emitcounts.js justify their
structure by saying the theory directory is not version controlled.
As of 2026-08-31 the parent repository whitelists zfs-rebase-theory/,
so it is.  That was the whole reason for keeping two copies of the
corpus.  Corrected in the comment where found; whether the split
should survive belongs to whoever owns the demo.

## Gotchas for the issues that follow

THE THREE HOLES ARE NOW FILLED, and the reason they existed is the
gotcha worth carrying forward.  VD7 (a directory child voting by its
own believed move), VD24 (composition through base) and the
claimed-guard realization case were all missing for one reason: every
fixture in the first sixteen was built to isolate ONE rule, and
isolating a rule means quieting everything that could interact with
it.  A corpus of clean single-purpose fixtures does not reach its own
composition rules by accident.  Expect the same gap shape in whatever
matrix comes next -- the interaction cells will be the empty ones,
and they will look covered because every neighbouring cell is.

op3-nested-believed, op3-compose-through-base and op6-claimed-guard
close them, all three authorized by the theory lane.  The last is the
only fixture in the set that separates THREE implementations rather
than two: unguarded, one-pass-guarded and two-pass-guarded each
produce a different gold section, and the middle one inverts which
pool materializes purely because names sort aa before y.

S1 IS DECIDABLE FROM op3-emigrant.  Its two arms separate all three
policies: unanimity blocks both folders, emigrants-abstain believes
both, majority believes one and blocks the other.  The cost of the
choice is only ever a DIRECTORY dnode -- red pairs every file pool
under all three dials.

## Commits

    827b57c  the first fourteen fixtures, hand-worked
    6548966  the reverse drift scan in sync-trees.sh
    42177c5  correspond.js and correspondcheck, twelve agreeing
    1e186d1  sixteen fixtures, op6 gold settled, the name-match fix

All on branch tree-suite of the harness repository, pushed.
