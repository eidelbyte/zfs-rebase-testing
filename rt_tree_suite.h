// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * The .tree suite: the parts of the fixture loader that need a pool
 * and an engine.  The parser itself is in rt_tree.h and deliberately
 * needs neither.
 *
 * Three pieces:
 *
 *   rt_tree_build.c   turns a parsed fixture into three real
 *                     datasets, which is the part that has to
 *                     understand that a fixture's "idx-txg" is a
 *                     statement about dnode SHARING, not about any
 *                     particular object number.
 *   rt_decision.c     the accessor shim: how the harness gets the
 *                     decision record back out of a run.  See the
 *                     contract below -- this is the piece the kernel
 *                     lane still owes, and the only one.
 *   rt_tree_check.c   compares gold to what came back.
 *
 * Polarity: onto is the substrate the output is built on, off-of is
 * the side whose changes are replayed, and the datasets are named
 * for those roles.  There is nothing to get backwards any more,
 * which is the point of the naming -- a fixture built into the wrong
 * side mirrors silently and still produces plausible output, so the
 * failure has to be made impossible rather than caught.
 *
 * For the same reason the parser refuses "left" and "right" as tree
 * names.  They are not the wrong way round; they carry no direction
 * at all, and a reader supplies whichever mapping they happen to
 * hold.  Two people who had both read this project closely turned
 * out to hold opposite ones, and each found the same fixture
 * plausible.  That is exactly how a mirrored fixture survives
 * review.
 *
 * The engine reads SNAPSHOTS, not live datasets: a snapshot has
 * committed its ZIL, so on-disk state equals logical state, and it
 * cannot change under the walk.  Materialization therefore ends by
 * snapshotting both sides.  Only the two sides are named; base is the
 * common ancestor of their snapshot chains and the engine discovers
 * it, so the suite builds RT_SNAP_BASE and then lets the engine find
 * it -- which incidentally means every fixture exercises ancestor
 * discovery for free.
 */

#ifndef	_RT_TREE_SUITE_H
#define	_RT_TREE_SUITE_H

#include "rebase_test.h"
#include "rt_tree.h"
#include "rt_tree_check.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	RT_SNAP_BASE	RT_DS_SRC "@base"
/*
 * The tag says what the snapshot IS, not which side it is on: the
 * dataset name already carries the role, and "offof@offof" would say
 * it twice.
 */
#define	RT_SNAP_OFFOF	RT_DS_OFFOF "@fixture"
#define	RT_SNAP_ONTO	RT_DS_ONTO "@fixture"

/*
 * The ONLY place the suite spells dsl_rebase()'s signature.  That
 * signature is still moving -- it lost a fence-post era, and gained
 * and then lost an explicit base argument, while this suite was
 * being written -- so it is worth exactly one function of
 * indirection.  outnvl may be NULL.
 */
int rt_engine_run(nvlist_t *outnvl);

/*
 * ------------------------------------------------------------------
 * Materialization
 * ------------------------------------------------------------------
 *
 * Creates the pool, builds the base tree in RT_DS_SRC, snapshots it,
 * clones RT_DS_OFFOF (off-of) and RT_DS_ONTO (onto), and applies each
 * side's delta.  On return the fixture is on disk and synced, ready
 * for a rebase.
 *
 * The identity mapping is the whole trick.  A fixture key such as
 * "20-100" is not an object number; it is the claim "these names
 * reach ONE dnode".  A key that base and a side share means the side
 * kept base's dnode, which cloning already provides for free.  A key
 * that appears only on one side means a fresh dnode there, and the
 * materializer lets dmu_object_alloc pick the number.  So the fixture
 * never dictates object numbers and never has to.
 *
 * Returns 0, or an errno with a sentence in errbuf.  ENOTSUP means
 * the fixture asks for something this materializer cannot build (a
 * directory with content, say); the caller should report the fixture
 * as skipped rather than failed, because nothing was proved either
 * way.
 */
int rt_tree_materialize(const rt_spec_t *sp, char *errbuf, size_t errlen);

/*
 * ------------------------------------------------------------------
 * The decision accessor: THE CONTRACT
 * ------------------------------------------------------------------
 *
 * WHAT THE SUITE NEEDS.  Per surviving name: did it survive, which
 * output pool holds it, and was that pool's component held back.  Per
 * output pool: file or directory, the bytes it decided on, and
 * whether it reuses an onto dnode or was materialized fresh.  Per
 * run: the conflict kinds raised.  That is the whole list, and it is
 * exactly the four things an expected tree asserts.
 *
 * There are two honest ways to supply it, and the suite does not care
 * which -- rt_decision_to_view() is the only code that would change.
 *
 *   (a) THE MANIFEST.  If the outnvl dsl_rebase() already fills makes
 *       those fields readable, nothing new is needed at all.  This is
 *       the better answer if the manifest is meant to be the reported
 *       result anyway, since it tests the thing users will see.
 *
 *   (b) AN INSPECTION SEAM.  sys/dsl_rebase.h says of
 *       rebase_decision_t: "the test interface: the harness asserts
 *       against this structure, never against scraped debug text."
 *       Every field above is already in it.  What is missing is any
 *       way to reach one, since the arena is released before the
 *       caller resumes.  The answer is not to extend the record's
 *       lifetime but to invert the call, so it never has to outlive
 *       the run:
 *
 *         typedef int (*rebase_inspect_cb_t)(
 *             const rebase_run_t *rr, void *arg);
 *         int dsl_rebase_inspect(const char *offof_snap,
 *             const char *onto_snap, rebase_inspect_cb_t cb,
 *             void *arg);
 *
 *       cb runs after the decide passes and before teardown.  The
 *       arena is alive inside it, so every pointer in the record is
 *       valid, nothing is copied or serialized, and no new lifetime
 *       rule enters the contract.  It is also the seam the manifest
 *       emitter wants anyway, which is what makes it worth building
 *       as contract rather than as test scaffolding.
 *
 *       This is why rt_decision_check() runs the whole comparison
 *       INSIDE the callback rather than handing a record back: a
 *       view built from the record points into the arena, and is
 *       worthless one instruction after the callback returns.
 *
 * Either way, three obligations matter more than the shape:
 *
 *   1. It reports what dsl_rebase() ACTUALLY DID -- same code path,
 *      not a reimplementation.  If the two can diverge, the suite is
 *      testing the wrong engine.
 *   2. It succeeds when the engine DECIDED, conflicts and all.  A
 *      conflict is a result, not a failure; a suite that cannot tell
 *      "the engine found a conflict" from "the engine broke" cannot
 *      test the conflict half of the theory at all, which is the half
 *      the whole corpus is about.
 *   3. Names come back resolvable to paths.  A name id the harness
 *      cannot turn back into "/single/journal.txt" cannot be matched
 *      against gold.
 *
 * Until one of them exists, rt_decision_available() returns 0 and the
 * suite runs its census tier, which is real but weak and says so on
 * every line it prints.
 */
int rt_decision_available(void);

/*
 * Run the engine and check this fixture's gold against the decision,
 * all within the engine's own inspection callback.  Returns 0 when
 * the run completed and res holds the verdict; an errno with a
 * sentence in errbuf when the engine could not decide at all.
 *
 * A conflicted run is a COMPLETED run and returns 0 -- the conflicts
 * are in res, where the fixture said they should be.
 */
int rt_decision_check(const rt_spec_t *sp, rt_check_result_t *res,
    char *errbuf, size_t errlen);

/*
 * ------------------------------------------------------------------
 * Checking
 * ------------------------------------------------------------------
 *
 * The comparison itself lives in rt_tree_check.c and works on the
 * flat rt_dview_t, so it can be tested without a pool.  This is the
 * adapter that fills one in from the real record; it is the only
 * code in the suite that reads rebase_decision_t, and is deliberately
 * thin for that reason.
 */
int rt_decision_to_view(const rebase_decision_t *rd, rt_dview_t *dv,
    char *errbuf, size_t errlen);
void rt_dview_free(rt_dview_t *dv);

/*
 * The weak tier: check gold against the census dbgmsg lines, which is
 * all that can be read without the accessor.  Counts cannot say WHICH
 * name survived or WHICH pool it landed in, so this proves much less
 * -- see the comment on the function for exactly what it does and
 * does not establish.
 */
void rt_tree_check_census(const rt_spec_t *sp, rt_check_result_t *res);

#ifdef	__cplusplus
}
#endif

#endif	/* _RT_TREE_SUITE_H */
