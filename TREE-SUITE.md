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

## What it can and cannot prove today

The comparison that matters is written and tested: name by name,
which survived, which share an output pool, what bytes it holds,
whether the dnode was reused. It lives in `rt_tree_check.c` and
`devcheck/checkcheck.c` proves it complains.

What is missing is anything to feed it. A decision has no external
view until the manifest carries one, so the suite runs the **census
tier**, off the engine's dbgmsg tallies. Those are counts. A count
can say five pools came out; it cannot say *which* names are in them.
It catches a fixture producing the wrong **number** of anything and
is blind to one producing the right number of the wrong things. Every
line says `CENSUS`.

That is a real gate rather than a placeholder, and it is what the
first box run should use. A failure in it is a statement about the
materializer or the fixture -- which is exactly what needs proving
before anything is asserted about the engine.

The census tier also only compares counts for fixtures gold says are
clean. A quarantined component is still decided, so its pools are
still in pass 2's tally, while the expected tree shows onto's
arrangement for that region instead. Comparing those two numbers
would be comparing different things, and would fail on a correct
engine.

## The contract this needs

The manifest is the only external view of a decision, which makes
completeness a **testability property**: any fact the engine computes
but the manifest drops is a fact no test can ever assert.

This is the consumer's list. It is short, because an expected tree
only asserts four things.

**Per surviving name**

- The full path, as text. Not an index into anything: a name the
  harness cannot turn back into `/single/journal.txt` cannot be
  matched against gold.
- **Which output pool holds it**, as a stable identifier. This is the
  field carrying the most weight and the easiest to leave out,
  because per-name it looks redundant. It is not. It is the only way
  to test that a hard link stayed one file -- two names sharing a
  pool and two names in separate pools look identical name by name.

The set must be **exhaustive**, because a fixture asserts deletion by
omission. A name gold does not list must not survive, and that is
uncheckable against a partial list.

**Per output pool**

- File or directory.
- **Realization**: does it reuse an onto dnode, or was it
  materialized fresh? This is what proves the substrate is not
  rewritten where it already agrees.
- The **source** of its content, as (tree, path) -- *not* the bytes.
  Naming the source keeps the manifest small and correct for real
  files, and it lets a fixture-based checker resolve the answer
  symbolically: "off-of's `/a/b`" is looked up in the already-parsed
  off-of tree, with no disk read at all.

**Per run**

- Each conflict's **kind** (lineage, name, pooling, content,
  structural). The certificate is welcome; the kind is what a fixture
  asserts.
- Which names came out **quarantined**, and ideally what held each
  one back. This is not a detail: a held-back component keeps onto's
  arrangement and has no output pool at all, so a checker that does
  not know which names those are will assert pooling and content
  about them and produce confident nonsense. This suite got that
  wrong once, and the corpus caught it.

**Wanted, not required:** the names that did *not* survive. Survival
is inferable from an exhaustive survivor list, but saying it outright
turns "gold lists it and the manifest does not" into a definite
statement rather than a two-possibility one.

Two obligations matter more than any field:

1. **It reports what the engine actually did** -- the same code path,
   not a parallel rendering. If the two can diverge, the suite is
   testing the wrong thing.
2. **It is produced when the engine decided, conflicts and all.** A
   conflict is a result, not a failure. A suite that cannot tell "the
   engine found a conflict" from "the engine broke" cannot test the
   conflict half of the theory -- the half the whole corpus is about.

### Structured, not rendered

The decided tree should reach this suite as **structured fields, not
as rendered `.tree` text**.

The checker compares semantically -- does this name survive, do these
two names share a pool -- against a fixture's `tree expected` block
that it has already parsed. Diffing rendered text against gold text
would be a worse comparison in three ways: it fails on ordering and
formatting differences that mean nothing, it cannot express the
quarantine exemption (a semantic rule about which assertions to
*skip*), and it forces the engine to own a renderer whose output is
load-bearing for tests.

A human-readable `.tree` rendering is worth having. It should be a
*view over* the structured data, produced by a userspace renderer,
not a field inside it -- which also settles the escaping problem,
since nothing then has to embed fixture syntax in a JSON string.

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

## When the manifest lands

The contract is `sprints/sprint-3/manifest-contract.md`. The mapping
into `rt_dview_t` is direct, which is the point of having written the
field list first:

    name path=          dvn_name
    name survived=      dvn_survives
    name held=          dvn_quarantined
    name pool=          dvn_outpool   (absent iff dead or held)
    pool type=          dvp_isdir
    pool realization=   dvp_materialized
    pool source_tree= + source_path=  resolved against the fixture's
                        own parsed tree -- no disk read
    conflict kind=      dv_conflict_kinds

Name records are exhaustive (one per name, deaths included with
`survived=no`), so "must not survive" is asserted against a stated
fact rather than an absence.

**Two encodings, one rule, and do not harmonize them.** A `.tree`
fixture *permits* over-escaping: `\141` is a legal if pointless
spelling of `a`, and `err-expect-escapes.tree` pins that. A manifest
*rejects* it, fatally, as a non-canonical escape.

That asymmetry is deliberate and load-bearing in one direction. A
manifest is machine-emitted and must be byte-exact between two
conforming emitters, so permitting redundant spellings would destroy
the guarantee its whole conformance section rests on. A fixture is
hand-written, where a redundant spelling is merely redundant. The
danger is a later reader noticing the difference and "fixing" it --
relaxing the manifest would silently retire canonical form.

The same applies to space. A `.tree` dump emits it literally, because
`name <path>` runs to end of line; a manifest emits `\040`, because
its values are whitespace-delimited. Same rule -- escape any byte the
surrounding grammar would otherwise consume -- applied to two
grammars.

**If this suite ever checks an applied result**, the metadata an
applier must reproduce is the engine's own compare set
(`rebase_identity_fixed[]` / `rebase_identity_var[]`): MODE, UID,
GID, FLAGS, RDEV, PROJID, SIZE, DACL_COUNT, DACL_ACES, SYMLINK, plus
xattrs. Anything missed there stops applied work reading back as
yellow-equal, so a re-run re-emits the same edit forever. That is
non-convergence, not untidiness, and it is why the list is longer
than the obvious four.

## Files

    rt_tree.h          parsed fixture types; no ZFS
    rt_tree_parse.c    the .tree grammar; no ZFS
    rt_tree_check.h    flat view of a decision, and the checker's API
    rt_tree_check.c    gold vs decision; no ZFS
    rt_tree_suite.h    the pool-facing API, and the contract above
    rt_tree_build.c    fixture to pool
    rt_engine.c        the one place the engine is called
    tree_suite.c       the driver and the census tier
    trees/             the corpus, plus MANIFEST
    devcheck/          the laptop gates and their fixtures
