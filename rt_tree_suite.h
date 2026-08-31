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
 *   rt_engine.c       the one place the engine is called.
 *   rt_tree_check.c   compares gold to what came back.
 *
 * What is NOT here is anything that reads a decision.  The manifest
 * is the external view of one, and until it carries the fields the
 * contract below asks for there is nothing to read -- so the suite
 * runs its census tier and no speculative adapter exists.
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
 * What the suite must be able to observe: THE CONTRACT
 * ------------------------------------------------------------------
 *
 * The output manifest is the only external view of a decision, which
 * makes completeness a TESTABILITY property: any fact the engine
 * computes but the manifest drops is a fact no test can ever assert.
 * This is the consumer's list, field by field.  It is short because
 * an expected tree only asserts four things.
 *
 * PER SURVIVING NAME
 *   - the full path, as text.  Not an index into anything: a name
 *     the harness cannot turn back into "/single/journal.txt" cannot
 *     be matched against gold.
 *   - which output pool holds it, as a stable identifier.  This is
 *     the one that carries the most weight and is easiest to leave
 *     out, because per-name it looks redundant.  It is not: it is
 *     the ONLY way to test that a hard link stayed one file.  Two
 *     names sharing a pool and two names in separate pools look
 *     identical name by name.
 *   The set of surviving names must be EXHAUSTIVE, because a fixture
 *   asserts deletion by omission -- a name gold does not list must
 *   not survive, and that is uncheckable against a partial list.
 *
 * PER OUTPUT POOL
 *   - file or directory.
 *   - realization: does it reuse an onto dnode, or was it
 *     materialized fresh?  This is what proves the substrate is not
 *     rewritten where it already agrees.
 *   - the SOURCE of its content, as (tree, path) -- NOT the bytes.
 *     Naming the source keeps the manifest small and correct for
 *     real files, and it lets a fixture-based checker resolve the
 *     answer symbolically: "off-of's /a/b" is looked up in the
 *     already-parsed off-of tree, with no disk read at all.  Bytes
 *     in the manifest would be wrong for real data and no better
 *     here.
 *
 * PER RUN
 *   - each conflict's KIND (lineage, name, pooling, content,
 *     structural).  The certificate is welcome but the kind is what
 *     a fixture asserts.
 *   - which names came out QUARANTINED, and ideally what held each
 *     one back.  Quarantine is not a detail: a held-back component
 *     keeps onto's arrangement, so it has no output pool at all, and
 *     a checker that does not know which names those are will assert
 *     pooling and content about them and produce confident nonsense.
 *     This suite got that wrong once and the corpus caught it.
 *
 * WANTED, NOT REQUIRED
 *   - the names that did NOT survive.  Survival can be inferred from
 *     an exhaustive survivor list, but a manifest that says so
 *     outright turns "gold lists it and the manifest does not" into
 *     a definite statement rather than a two-possibility one.
 *
 * Two obligations matter more than any field:
 *
 *   1. It reports what the engine ACTUALLY DID -- the same code
 *      path, not a parallel rendering.  If the two can diverge, the
 *      suite is testing the wrong thing.
 *   2. It is produced when the engine DECIDED, conflicts and all.  A
 *      conflict is a result, not a failure; a suite that cannot tell
 *      "the engine found a conflict" from "the engine broke" cannot
 *      test the conflict half of the theory, which is the half the
 *      whole corpus is about.
 *
 * Until the manifest carries this, the suite runs its census tier,
 * which is real but weak and says so on every line it prints.
 */

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
