# The .tree suite

`tree_suite` is a second test binary beside `m1_smoke`. The smoke
tracks the engine's milestones and moves when a pass lands. This one
tracks the fixture corpus: it reads `.tree` files, builds the trees
they describe on a real pool, runs the engine, and checks what came
out against the answer the fixture carries.

    make tree_suite && sudo ./tree_suite
    sudo ./tree_suite trees/open-problems/5-rename-rename.tree

With no arguments it runs everything under `trees/`.

It does not touch `m1_smoke.c` and shares only the harness runtime
(`rt_harness.c`, `rt_scaffold.c`, `rt_zpl.c`) with it.

## Why a fixture is one file

A `.tree` file states a whole scenario: the three input trees, the
output they should decide to, and the handful of things an output
tree cannot say about itself. The same files are read by the
reference engine in JavaScript, so a scenario and its expectation are
one artifact rather than two that can drift apart.

Here is the whole of open problem 5, trimmed:

    tree base
      1-1: /
        2-100: single/
          10-100: notes.txt = notes-v1

    tree onto
      1-1: /
        2-100: single/
          10-100: journal.txt = notes-v1

    tree offof
      1-1: /
        2-100: single/
          10-100: diary.txt = notes-v1

    expect conflict name
    expect quarantined /single/journal.txt

    tree expected
      1-1: /
        2-100: single
          10-100: journal.txt = notes-v1

`10-100` is not an object number and never becomes one. It is the
claim "every line wearing this key reaches one dnode". That is the
whole trick of the format, and it is what lets a fixture describe
hard links, renames and identity without knowing anything about how
ZFS will lay them out.

## What the expected tree asserts

Four things at once, which is why it is a tree and not a list.

**Survival, exhaustively.** A name it lists must survive; a name it
omits must not. There is no "absent" directive because the tree
already says so. This is the strongest property of the format: a
fixture cannot quietly forget to assert a deletion.

**Pooling.** Names sharing a key must land in one output pool, and
names with different keys must not share one. A hard link that comes
out as two separate files is the bug this catches, and nothing about
either file on its own looks wrong.

**Content and type**, from the token and the shape.

**Realization.** A key that onto also uses asserts the output reuses
that dnode. A key onto does not have asserts a fresh one was made.
Rewriting the substrate where it already agrees is exactly the waste
the rule exists to forbid.

Quarantine is exempt from the last three, and this is not a
convenience. A held-back component keeps onto's arrangement
untouched, so it has no output pool at all -- there is nothing there
to assert pooling, content or realization about. What a quarantined
region *can* be asserted about is that it was held back, which is
what `expect quarantined` is for.

## The two tiers, and which one is running

The suite prints the tier on its first line and on every passing
fixture, because the difference is large.

**Gold tier** is the real one. It asserts, name by name, which names
survived, which share an output pool, what bytes that pool holds, and
whether the dnode was reused. It needs a way to read the engine's
decision back, which does not exist yet.

**Census tier** is what runs today. It reads the engine's dbgmsg
tallies, which are counts. A count can say five pools came out; it
cannot say *which* names are in them. So the census tier catches a
fixture that produces the wrong **number** of anything and is blind
to one that produces the right number of the wrong things. Its lines
are labelled `CENSUS` so nobody mistakes them for the real thing.

The census tier also only compares counts for fixtures gold says are
clean. A quarantined component is still decided, so its pools are
still in pass 2's tally, while the expected tree shows onto's
arrangement for that region instead. Comparing those two numbers
would be comparing different things, and would fail on a correct
engine.

## The contract this needs

Everything above is built. One thing is missing, and it is the only
thing.

**What the suite needs, per run:** for each surviving name, whether
it survived, which output pool holds it, and whether its component
was held back. For each output pool, whether it is a file or a
directory, the bytes it decided on, and whether it reuses an onto
dnode or was materialized fresh. And the conflict kinds raised.

That list is exactly the four things an expected tree asserts, and
nothing more.

There are two honest ways to supply it. The suite does not care
which; `rt_decision_to_view()` in `rt_decision.c` is the only code
that would change, and it is deliberately thin for that reason.

**(a) The manifest.** If the `outnvl` that `dsl_rebase()` already
fills makes those fields readable, nothing new is needed. This is the
better answer if the manifest is meant to be the reported result
anyway, because then the suite tests the thing users will actually
see.

**(b) An inspection seam.** `sys/dsl_rebase.h` says of
`rebase_decision_t`: *"the test interface: the harness asserts
against this structure, never against scraped debug text."* Every
field listed above is already in it. What is missing is any way to
reach one, since the arena is released before the caller resumes.

The answer is not to make the record outlive the run -- it is to
invert the call so it never has to:

    typedef int (*rebase_inspect_cb_t)(const rebase_run_t *rr,
        void *arg);
    int dsl_rebase_inspect(const char *offof_snap,
        const char *onto_snap, rebase_inspect_cb_t cb, void *arg);

`cb` runs after the decide passes and before teardown. The arena is
alive inside it, so every pointer is valid, nothing is copied or
serialized, and no new lifetime rule enters the contract. It is also
the seam the manifest emitter wants anyway, which is what makes it
worth building as contract rather than as test scaffolding.

That lifetime is why `rt_decision_check()` runs the whole comparison
*inside* the callback rather than handing a view back out: the view
points into the arena and is worthless one instruction later.

Three obligations matter more than the shape:

1. **It reports what `dsl_rebase()` actually did** -- the same code
   path, not a reimplementation. If the two can diverge, the suite is
   testing the wrong engine.
2. **It succeeds when the engine decided, conflicts and all.** A
   conflict is a result, not a failure. *(Settled: disposition is a
   field in the output, never the return code. Nonzero means the
   engine could not decide.)*
3. **Names come back resolvable to paths.** A name id the harness
   cannot turn back into `/single/journal.txt` cannot be matched
   against gold.

When the seam lands, build with
`CPPFLAGS+=-DRT_HAVE_DECIDE_ACCESSOR` and the suite switches tiers.
Nothing else changes -- and that path is already compiled today, so
it will not have rotted in the meantime.

## How a fixture becomes a pool

`rt_tree_build.c`. Base is built into `rtest/src` and snapshotted;
both sides are cloned from that snapshot, so a key shared between
base and a side is *already* the same dnode with no work. Then each
side gets a delta, derived rather than written down -- the fixture
states two end states and the materializer works out the operations
between them.

That is deliberate. A fixture that spelled out operations would be
asserting the harness's idea of what happened rather than the
scenario, and the two drift.

The delta runs in three phases: placements shallowest-first (so a
directory exists before anything goes in it, and a directory's own
move happens before its children are considered), then removals
deepest-first (so a directory is empty when its entry goes), then
content last (so a rename is never mistaken for an edit). A name the
target wants but the wrong dnode occupies is moved aside to a scratch
name and collected when its own key's turn comes -- the same trick
the engine uses for rotation cycles, and for the same reason: without
it, two names that swap places cannot be built at all.

Finally both sides are snapshotted, because the engine is read-only
and rejects a live head with `EINVAL` -- it creates nothing, so the
caller supplies the immutability. Snapshotting is part of *building*
the fixture rather than part of running it, which is where it
belongs: a fixture is pinned once it is built, and every later
decision reads the same thing.

Base is *not* passed to the engine. It discovers the common ancestor
from the two snapshot chains, which means every fixture in the corpus
exercises that discovery for free.

Two things the materializer refuses rather than fakes, reported as
`SKIP` so nothing is silently untested: a directory carrying a
content token (the harness has no way to set directory attributes;
no corpus fixture does this), and a fixture that gives the root a
different identity than base (the root dnode cannot be replaced).

## Polarity

Onto is the substrate the output is built on; off-of is the side
whose changes are replayed. The datasets are named for those roles,
`RT_DS_ONTO` and `RT_DS_OFFOF`, so there is nothing left to get
backwards.

`left` and `right` are refused as tree names. Not because they are
the wrong way round -- **because they carry no direction at all.**

That distinction cost this project an argument, and it is the useful
part. The reference parser used to accept them as aliases. Two people
who had both read the project closely turned out to hold *opposite*
mappings of what `left` meant, and each found the same fixture
plausible under their own reading. There was never a correct
direction to preserve, so the fix is to delete the words rather than
to reverse them -- reversing them would just relocate the ambiguity.

This is exactly how a mirrored fixture survives review: the reader
supplies the mapping, so the document agrees with whoever is reading
it. That is worse than "it still looks plausible" -- the fixture is
not merely plausible to a reader holding the wrong mapping, it is
affirmatively confirming to them. A loader that silently mirrors a
fixture is the worst failure a loader can have.

Both parsers refuse those names now, **in the same words**, and
`err-alias.tree` is in the cross-parser diff so it stays that way.
There is no exempt set: that exemption existed for exactly this case,
and it went away by the two sides agreeing rather than by the check
being relaxed.

The advice to write `onto` or `offof` belongs only to those two
names. A merely unknown tree name gets the generic message, or the
advice becomes noise attached to every typo --
`err-unknown-tree.tree` pins that, and is diffed alongside.

Worth knowing: the older `zfs-rebase-theory/examples/*.tree` corpus
that `lpgraph.py` reads **is** written in left/base/right. That tool
predates the onto/off-of distinction and never uses those words, so
there was never a correct mapping to preserve -- those documents need
a human to relabel them. Pointing `tree_suite` at them will
(correctly) refuse them.

## What is checked on a laptop

Most of this suite is testable without a box, and is tested there.

    devcheck/treecheck.sh     parser agreement + checker self-test
    devcheck/suitecheck.sh    syntax and arity for the ZFS-facing files

`treecheck.sh` does four things. It verifies the corpus has not
drifted from the demo's copy (`trees/MANIFEST`). It builds the C
parser and dumps all 24 fixtures -- the 11 real ones plus 13
deliberately broken ones under `devcheck/treecases/` -- into a
canonical form. It runs the reference parser over the same files and
**diffs the two**, so "the two implementations agree" is a checked
claim rather than an intention, error messages included. Nothing is
exempt from that diff. And it runs
`checkcheck`, which builds decision views by hand and requires the
gold checker to complain at each of them: wrong content, split
pooling, merged identities, an unlisted survivor, a lost name, a
verdict that was not clean, a needless materialization, and an unmet
quarantine claim. A checker that only ever passes is worthless, and a
checker is the one piece of test machinery nobody tests.

`suitecheck.sh` compiles the three files that need a pool against the
**real** `sys/dsl_rebase.h`, with only the harness API stubbed. Using
the real contract header rather than a fake is the point: a fake
would make the check agree with itself instead of with the engine.
It compiles them **both ways**, with and without
`RT_HAVE_DECIDE_ACCESSOR`, because the inspection-seam path is dead
code until the seam lands and would otherwise rot unwatched -- and it
is exactly the code that has to be right on the day it switches on.

This gate has already earned its keep: it caught the `dsl_rebase()`
signature changing under the suite twice while it was being written.

One failure it cannot catch, and which looks like a corpus disaster:
a libzpool from before the engine went read-only links fine, because
that rewrite did not change `dsl_rebase()`'s arity, and then rejects
the snapshots it is handed. Every fixture fails identically with
`EINVAL`. The suite watches for that shape and says so rather than
letting you read it as a broken corpus.

Neither gate can catch a wrong answer. The FreeBSD build and run
remain the authority.

## Files

    rt_tree.h          parsed fixture types; no ZFS
    rt_tree_parse.c    the .tree grammar; no ZFS
    rt_tree_check.h    flat view of a decision, and the checker's API
    rt_tree_check.c    gold vs decision; no ZFS
    rt_tree_suite.h    the pool-facing API, and the contract above
    rt_tree_build.c    fixture to pool
    rt_decision.c      the accessor shim and the view adapter
    tree_suite.c       the driver and the census tier
    trees/             the corpus, plus MANIFEST
    devcheck/          the laptop gates and their fixtures
