# Eight fixtures that want a person to look at them

Read-only copies, put here so they can be read without any generator
touching them.  Written 2026-08-31.

## Why this directory exists, and why it is outside trees/

Three globs would otherwise pick these files up, and each would do
something unwanted:

  decide-demo/gen-expected.py   globs examples/*.tree and
                                examples/*/*.tree and WRITES THE
                                FILES BACK.  Its gold is derived from
                                the reference engine, which is the
                                one thing these fixtures must not be.
  devcheck/sync-trees.sh        globs trees/*.tree and trees/*/*.tree
                                to build trees/MANIFEST.  A copy here
                                would be manifested as a second
                                fixture.
  devcheck/treecheck.sh         parses trees/ and devcheck/treecases/
                                through both parsers.  A copy would
                                be parsed twice and counted twice.

review/ is in none of them, deliberately.

## These are COPIES.  The originals are authoritative

    trees/open-problems/<same name>

Do not edit the copies.  If a decision changes a fixture, change the
original, then refresh here.  Checksums below make a stale copy
visible rather than silent -- two copies of anything drift, and this
project has already been bitten once by a gate that watched only one
direction of that drift.

    1798210016-2937  op3-dir-merge-fanin.tree
    4029133834-4733  op3-emigrant.tree
    669532104-6352   op3-nested-recreated.tree
    2511098208-3427  op3-severed-cross-parent.tree
    2361059725-3453  op6-fragment-multi.tree
    4032296893-2669  op6-one-move-deltas.tree
    3466978906-3081  op6-straddle-split.tree
    3317053217-3000  op6-wholesale-quiet.tree

Verify with:  cksum review/needs-a-decision/*.tree

## What to decide, one file at a time

Each fixture's own comment carries the full reasoning and a WORKED
OUTCOME block.  This is only the question, so you can pick which ones
are worth your time.

### op3-emigrant.tree -- THE BIG ONE

Successor question S1.  A directory is rebuilt somewhere else and
some of its children go elsewhere instead.  Does one child leaving
block the move?

Two folders, built so no two policies agree:

    policy                  one-out/     two-out/
    unanimity (adopted)     blocked      blocked
    emigrants abstain       moved        moved
    majority                moved        blocked

WHAT IT COSTS EITHER WAY, so the decision is not made on fear: red
pairs every FILE pool under all three dials, so no file materializes
and no content moves anywhere surprising.  The whole difference is
whether a DIRECTORY keeps its dnode.  Unanimity is the conservative
reading and matches the document's exact-evidence character; it also
throws away a directory identity because somebody moved one file out
of a folder they were reorganising, which is not a rare thing to do.

The gold currently records unanimity.

### op6-fragment-multi.tree and op6-wholesale-quiet.tree

These two FAIL against the engine as it stands today, on purpose.
Their gold is the settled answer and the engine has not been rebuilt
yet, so they go green when wave 2 lands.

The question is whether you want a red suite in the meantime, or
whether these should be held out somewhere until the kernel catches
up.  I chose red, on the grounds that a fixture quietly excluded is a
fixture nobody looks at.  It is a choice, not a derivation.

wholesale-quiet is the more interesting of the two: its tree shape is
IDENTICAL before and after the settlement and only the dnode differs.
That is the silent-materialize defect exactly -- the output looks
completely correct to anything reading names alone.

### op3-dir-merge-fanin.tree

Successor question S3.  Two directories fully merge into one.  Both
moves are believed, which is settled and not in question.  What is
open: the merged directory reuses ONE of onto's two dnodes, and
nothing yet says which.

The gold guesses the lower-sorted base name, so /a's dnode survives
and /c's does not.  IF THE ENGINE PICKS THE OTHER, MY GOLD IS WRONG
AND THE ENGINE IS NOT.  Worth pinning deliberately rather than
discovering.

### op6-straddle-split.tree and op6-one-move-deltas.tree

Both conflict, and the conflict is right in both.  The uncertain part
is narrower: the conflict KIND and the exact set of quarantined
paths.

I took both by analogy with fixtures the engine has actually run --
6-ambiguous-succession.tree and 5-rename-rename.tree -- rather than
deriving them from the rules, because the rules do not state them.
These are the lowest-confidence lines in the whole set of nineteen,
and they are flagged as such in both files.  If the engine disagrees
here, believe the engine.

### op3-severed-cross-parent.tree and op3-nested-recreated.tree

Both refuse a move that a person reading the trees would probably
call obvious, and both are asserted as CORRECT.  That is the pair
most worth a second opinion, because the fixtures are only as good as
the judgement that the price is right.

  op3-severed-cross-parent   A directory is rebuilt under a DIFFERENT
                             parent, same leaf, same file bytes.  No
                             candidate is ever nominated, so nothing
                             is even considered -- the red anchor is
                             dead and the sibling generator cannot
                             see across pairs.  This is the shape the
                             theory knowingly gave up when yellow
                             stopped nominating candidates.

  op3-nested-recreated       A rival target exists that only the
                             sibling generator finds, and only after
                             the parent pair is on the docket.  Two
                             phases refuse the move.  A naive
                             one-phase engine would believe it, and
                             would be right by luck -- the rival is
                             real and the early belief was made
                             against an incomplete docket.

In both, the cost is a directory dnode and nothing else: files keep
their identities and their content either way.

## The one item that is not a fixture

Sixth on the list and it has no .tree file: succession.js takes its
deltas as hand-supplied INPUT rather than deriving them, because
attributing a lost name to the right fragment of a split pool needs
the component and reimplementing pairing there would be a worse copy
of the engine rather than a second opinion.  The consequence is that
a wrong hand-derivation passes silently.  It is stated at the top of
succession.js; it is a structural limit, not a fixable one at this
layer.

## The standing caveat

No engine has run any of these.  Survival, pooling, content and
realization are hand-worked claims throughout.  What has executed is
the correspondence phase (correspond.js, fourteen fixtures agreeing)
and the succession rule on supplied deltas (succession.js, ten
agreeing).
