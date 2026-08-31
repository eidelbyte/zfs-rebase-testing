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
 * Polarity, stated once and relied on everywhere: ONTO is the
 * substrate and lives in RT_DS_RIGHT; OFF-OF is the replayed side and
 * lives in RT_DS_LEFT.  Getting this backwards mirrors every fixture
 * and still produces plausible-looking output, which is why the
 * parser refuses the "left"/"right" tree aliases outright.
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
#define	RT_SNAP_OFFOF	RT_DS_LEFT "@offof"
#define	RT_SNAP_ONTO	RT_DS_RIGHT "@onto"

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
 * clones RT_DS_LEFT (off-of) and RT_DS_RIGHT (onto), and applies each
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
 *   (b) A RECORD ACCESSOR.  sys/dsl_rebase.h says of
 *       rebase_decision_t: "the test interface: the harness asserts
 *       against this structure, never against scraped debug text."
 *       Every field above is already in it.  What is missing is any
 *       way to obtain one, since the arena is released before the
 *       caller is resumed.  In libzpool only, no ioctl surface:
 *
 *         int dsl_rebase_decide_test(const char *offof_snap,
 *             const char *onto_snap, const char *base_snap,
 *             rebase_decision_t **rdp, void **cookiep);
 *         void dsl_rebase_decide_test_free(void *cookie);
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
int rt_decision_get(const rebase_decision_t **rdp, void **cookiep);
void rt_decision_put(void *cookie);

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
