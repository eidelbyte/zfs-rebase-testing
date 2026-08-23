// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Membership-merge matrix (U unification, R resolution) tests --
 * see TEST-MATRIX.md, "Membership-merge matrix". Each test's
 * comment names the cell it covers.
 *
 * Cross-reference phases C and D are observed through the finals
 * and conflicts summary lines via rt_final_stats() and
 * rt_conflict_stats(). Every test asserts the 12-value expectation
 * (seven finals buckets, five conflict counters); mm_finish() also
 * scrapes the phase B target lines to check the row-count
 * invariant -- the finals buckets must sum to the row count --
 * structurally on every test. Earlier-phase counters are owned by
 * their own sections. Unification is visible through the conflict
 * bucket: an un-unified shared row is either left undecided
 * (both-NOVEL) or divergent (fragments), so zero conflicts on an
 * overlapping fixture proves the ids merged.
 */

#include "rebase_test.h"

/*
 * Expected 12-tuple for one U/R fixture. Finals order is (same,
 * gone, standalone, anchor, fragment, novel, conflict); conflict
 * counters are (total, relink, divergent, overlap, content).
 * Designated initializers keep unexercised counters at zero.
 */
typedef struct mm_expect {
	uint64_t	mme_final[7];
	uint64_t	mme_total, mme_relink, mme_divergent,
			mme_overlap, mme_content;
} mm_expect_t;

static void
mm_print(const char *tag, const uint64_t f[7],
    const rt_conflict_stats_t *cs)
{
	(void) printf("    [%s] finals "
	    "(%llu,%llu,%llu,%llu,%llu,%llu,%llu) conflicts "
	    "t=%llu r=%llu d=%llu o=%llu c=%llu\n", tag,
	    (unsigned long long)f[0], (unsigned long long)f[1],
	    (unsigned long long)f[2], (unsigned long long)f[3],
	    (unsigned long long)f[4], (unsigned long long)f[5],
	    (unsigned long long)f[6],
	    (unsigned long long)cs->rcs_total,
	    (unsigned long long)cs->rcs_relink,
	    (unsigned long long)cs->rcs_divergent,
	    (unsigned long long)cs->rcs_overlap,
	    (unsigned long long)cs->rcs_content);
}

/*
 * Sync, run the rebase (expecting success), scrape the
 * targets, finals, and conflicts lines, tear the pool down, and
 * compare the 12-tuple. The row-count invariant (T8's cousin):
 * the finals buckets must sum to the row count reported by either
 * side's target tallies.
 */
static int
mm_finish(const mm_expect_t *e)
{
	nvlist_t *nvl;
	rt_target_stats_t ts;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	uint64_t rows = 0, sumf = 0;
	boolean_t bad = B_FALSE;
	int err, i;

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err != 0) {
		rt_scaffold_teardown();
		(void) printf("\n    [mm] rebase failed: %s (%d)\n",
		    strerror(err), err);
		return (1);
	}
	fnvlist_free(nvl);

	err = rt_target_stats(&ts);
	if (err == 0)
		err = rt_final_stats(&fs);
	if (err == 0)
		err = rt_conflict_stats(&cs);
	rt_scaffold_teardown();
	if (err != 0) {
		(void) printf("\n    [mm] no summary lines (%d)\n",
		    err);
		return (1);
	}

	for (i = 0; i < 6; i++)
		rows += ts.rts_left[i];
	for (i = 0; i < 7; i++)
		sumf += fs.rfs_kind[i];
	if (rows != sumf) {
		(void) printf("\n    [mm] row-count invariant "
		    "broken: rows %llu != finals sum %llu\n",
		    (unsigned long long)rows,
		    (unsigned long long)sumf);
		return (1);
	}

	for (i = 0; i < 7; i++) {
		if (fs.rfs_kind[i] != e->mme_final[i])
			bad = B_TRUE;
	}
	if (cs.rcs_total != e->mme_total ||
	    cs.rcs_relink != e->mme_relink ||
	    cs.rcs_divergent != e->mme_divergent ||
	    cs.rcs_overlap != e->mme_overlap ||
	    cs.rcs_content != e->mme_content)
		bad = B_TRUE;

	if (bad) {
		rt_conflict_stats_t ecs;

		ecs.rcs_total = e->mme_total;
		ecs.rcs_relink = e->mme_relink;
		ecs.rcs_divergent = e->mme_divergent;
		ecs.rcs_overlap = e->mme_overlap;
		ecs.rcs_content = e->mme_content;
		(void) printf("\n");
		mm_print("expected", e->mme_final, &ecs);
		mm_print("got", fs.rfs_kind, &cs);
		return (1);
	}
	return (0);
}

/*
 * Base builders. mm_base_pool populates src with one linkpool of
 * the given member names (first name owns the content), snapshots,
 * and clones. Content "x" unless told otherwise.
 */
static int
mm_base_pool(const char *const names[], uint_t n,
    const char *data, uint_t datalen, uint64_t *objp)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	err = rt_open(RT_DS_SRC, &d);
	if (err != 0)
		return (err);
	err = rt_create_file(d.rtd_os, d.rtd_root, names[0], data,
	    datalen, &obj);
	for (uint_t i = 1; err == 0 && i < n; i++)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root,
		    names[i], obj);
	rt_close(&d);
	if (err == 0 && objp != NULL)
		*objp = obj;
	return (err);
}

/*
 * Sever the named members of a base pool on one side into a new
 * shared dnode carrying the given content: remove the entries,
 * create the first name fresh, hardlink the rest to it.
 */
static int
mm_sever_set(const char *dsname, const char *const names[],
    uint_t n, const char *data, uint_t datalen, uint64_t *zp)
{
	rt_ds_t d;
	uint64_t z;
	int err;

	err = rt_open(dsname, &d);
	if (err != 0)
		return (err);
	err = 0;
	for (uint_t i = 0; err == 0 && i < n; i++)
		err = rt_remove_entry(d.rtd_os, d.rtd_root,
		    names[i]);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, names[0],
		    data, datalen, &z);
	for (uint_t i = 1; err == 0 && i < n; i++)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root,
		    names[i], z);
	rt_close(&d);
	if (err == 0 && zp != NULL)
		*zp = z;
	return (err);
}

/*
 * Create a fresh two-member pool (create the first name, link the
 * second) on one side.
 */
static int
mm_novel_pair(const char *dsname, const char *n1, const char *n2,
    const char *data, uint_t datalen, uint64_t *objp)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	err = rt_open(dsname, &d);
	if (err != 0)
		return (err);
	err = rt_create_file(d.rtd_os, d.rtd_root, n1, data,
	    datalen, &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, n2,
		    obj);
	rt_close(&d);
	if (err == 0 && objp != NULL)
		*objp = obj;
	return (err);
}

/*
 * U1: the doc's worked example -- left splits {b,c} out of a
 * four-member pool, right splits {c,d}. The shared path c proves
 * unification (an un-unified pair would make c's row divergent);
 * the merged fragment roster is {b,c,d}.
 */
static int
test_merge_fragment_overlap(void)
{
	static const char *const names[] = { "a", "b", "c", "d" };
	static const char *const lsev[] = { "b", "c" };
	static const char *const rsev[] = { "c", "d" };
	mm_expect_t e = {
		.mme_final = { 1, 0, 0, 0, 3, 0, 0 },
	};

	TEST_START("U1: overlapping fragments unify");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 4, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_sever_set(RT_DS_LEFT, lsev, 2, "x", 1, NULL),
	    "left sever");
	RT_CHECK(mm_sever_set(RT_DS_RIGHT, rsev, 2, "x", 1, NULL),
	    "right sever");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U2: zero-overlap fragments of one parent stay separate (rule
 * 5): all four severed paths land in the fragment bucket with
 * zero conflicts. Group distinctness is invisible in tallies and
 * deferred to emit.
 */
static int
test_merge_fragment_disjoint(void)
{
	static const char *const names[] =
	    { "a", "b", "c", "d", "e" };
	static const char *const lsev[] = { "b", "c" };
	static const char *const rsev[] = { "d", "e" };
	mm_expect_t e = {
		.mme_final = { 1, 0, 0, 0, 4, 0, 0 },
	};

	TEST_START("U2: disjoint fragments stay separate");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 5, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_sever_set(RT_DS_LEFT, lsev, 2, "x", 1, NULL),
	    "left sever");
	RT_CHECK(mm_sever_set(RT_DS_RIGHT, rsev, 2, "x", 1, NULL),
	    "right sever");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U3: a fragment meets the two-parent decline. Left fragments
 * {c,d} out of P1. Right builds a pool from P1's c plus P2's y --
 * two moved-from parents, so phase A declined it to NOVEL (A8).
 * The shared row c is FRAGMENT vs NOVEL: DIVERGENT_MEMBERSHIP.
 * True cross-parent fragment-vs-fragment is unrepresentable (see
 * the matrix preamble).
 */
static int
test_merge_fragment_vs_novel(void)
{
	static const char *const p1[] = { "a", "b", "c", "d" };
	static const char *const p2[] = { "y", "z" };
	static const char *const lsev[] = { "c", "d" };
	rt_ds_t d;
	uint64_t z;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 0, 1, 0, 1, 1, 1 },
		.mme_total = 1,
		.mme_divergent = 1,
	};

	TEST_START("U3: fragment vs two-parent-declined novel");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(p1, 4, "x", 1, NULL), "base P1");
	RT_CHECK(mm_base_pool(p2, 2, "w", 1, NULL), "base P2");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_sever_set(RT_DS_LEFT, lsev, 2, "x", 1, NULL),
	    "left sever");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "y"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "c"));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "y", "w", 1,
	    &z));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "c", z);
	rt_close(&d);
	RT_CHECK(err, "right two-parent pool");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U4: overlapping novel pools with identical data unify -- the
 * shared row resolves NOVEL instead of stalling undecided.
 */
static int
test_merge_novel_unified(void)
{
	mm_expect_t e = {
		.mme_final = { 0, 0, 0, 0, 0, 3, 0 },
	};

	TEST_START("U4: overlapping novels, same data, unify");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_novel_pair(RT_DS_LEFT, "x", "y", "n", 1, NULL),
	    "left pool");
	RT_CHECK(mm_novel_pair(RT_DS_RIGHT, "y", "w", "n", 1, NULL),
	    "right pool");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U5: overlapping novel pools with DIFFERENT data conflict,
 * scoped to exactly the overlapping path; the non-overlapping
 * members still resolve NOVEL.
 */
static int
test_merge_novel_overlap_conflict(void)
{
	mm_expect_t e = {
		.mme_final = { 0, 0, 0, 0, 0, 2, 1 },
		.mme_total = 1,
		.mme_overlap = 1,
	};

	TEST_START("U5: overlapping novels, diff data, conflict");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_novel_pair(RT_DS_LEFT, "x", "y", "AAA", 3,
	    NULL), "left pool");
	RT_CHECK(mm_novel_pair(RT_DS_RIGHT, "y", "w", "BBB", 3,
	    NULL), "right pool");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U6: zero-overlap novel pools stay separate even with identical
 * data (rule 5: content is not evidence of shared intent).
 */
static int
test_merge_novel_disjoint(void)
{
	mm_expect_t e = {
		.mme_final = { 0, 0, 0, 0, 0, 4, 0 },
	};

	TEST_START("U6: disjoint novels stay separate");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_novel_pair(RT_DS_LEFT, "x", "y", "n", 1, NULL),
	    "left pool");
	RT_CHECK(mm_novel_pair(RT_DS_RIGHT, "w", "v", "n", 1, NULL),
	    "right pool");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U7: chained unification -- two left pools bridged by one right
 * pool collapse into a single group. A broken chain would leave
 * the two bridge rows undecided (conflict bucket 2, not 0).
 */
static int
test_merge_novel_chain(void)
{
	mm_expect_t e = {
		.mme_final = { 0, 0, 0, 0, 0, 4, 0 },
	};

	TEST_START("U7: chained novels form one group");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_novel_pair(RT_DS_LEFT, "a", "b", "n", 1, NULL),
	    "left pool one");
	RT_CHECK(mm_novel_pair(RT_DS_LEFT, "c", "d", "n", 1, NULL),
	    "left pool two");
	RT_CHECK(mm_novel_pair(RT_DS_RIGHT, "b", "c", "n", 1, NULL),
	    "right bridge");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U8: both sides recycled the same pool dnode identically (gen
 * flips, content untouched). RECYCLED pools enter the heuristic
 * like novels; identical data over the same paths unifies.
 */
static int
test_merge_recycled_unified(void)
{
	static const char *const names[] = { "f", "g" };
	rt_ds_t d;
	uint64_t obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 0, 0, 0, 0, 2, 0 },
	};

	TEST_START("U8: both sides recycled alike, unify");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 2, "x", 1, &obj), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_set_sa_u64(d.rtd_os, obj, ZPL_GEN, 111111);
	rt_close(&d);
	RT_CHECK(err, "flip left gen");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_set_sa_u64(d.rtd_os, obj, ZPL_GEN, 222222);
	rt_close(&d);
	RT_CHECK(err, "flip right gen");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * U9: a data-compare fault during novel matching is EIO. The
 * malformed DXATTR blob sits on the right pool's dnode, which
 * nothing earlier reads (its paths are plain ADDs, and phase A
 * classifies it NOVEL without touching gen or xattrs): phase C is
 * the first reader.
 */
static int
test_merge_data_fault(void)
{
	static const uchar_t garbage[] = {
		0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef
	};
	rt_ds_t d;
	uint64_t robj;
	int err;

	TEST_START("U9: data-compare fault = EIO");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_novel_pair(RT_DS_LEFT, "x", "y", "q", 1, NULL),
	    "left pool");
	RT_CHECK(mm_novel_pair(RT_DS_RIGHT, "y", "w", "q", 1,
	    &robj), "right pool");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_set_sa_blob(d.rtd_os, robj, ZPL_DXATTR, garbage,
	    sizeof (garbage));
	rt_close(&d);
	RT_CHECK(err, "write garbage blob");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EIO, "expected EIO");
	TEST_PASS();
}

/*
 * R1: THE ACCEPTANCE CASE. Base {A,B,C}; left severs A; right does
 * nothing. The sever wins with no conflict and no union, and A is
 * not resurrected into the linkpool.
 */
static int
test_merge_sever_acceptance(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 0, 1, 0, 0, 0, 0 },
	};

	TEST_START("R1: sever wins, no resurrection (acceptance)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever A");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R2: both sides sever the same path -- agreement, standalone,
 * zero conflicts.
 */
static int
test_merge_sever_agreement(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 0, 1, 0, 0, 0, 0 },
	};

	TEST_START("R2: both sever the same path, agree");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever A left");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever A right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R3: a lone delete of a pooled member wins -- GONE, survivors
 * silent, zero conflicts.
 */
static int
test_merge_lone_gone(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 1, 0, 0, 0, 0, 0 },
	};

	TEST_START("R3: lone GONE wins");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	rt_close(&d);
	RT_CHECK(err, "unlink C");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R4: GONE vs STANDALONE is the delete-vs-sever near-equivalence;
 * under REBASE_POLICY_NONE it surfaces as DELETE_VS_RELINK and the
 * row stays undecided. The dissolved pool's mate agrees on
 * STANDALONE from both sides.
 */
static int
test_merge_gone_vs_standalone(void)
{
	static const char *const names[] = { "f", "g" };
	rt_ds_t d;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 0, 1, 0, 0, 0, 1 },
		.mme_total = 1,
		.mme_relink = 1,
	};

	TEST_START("R4: GONE vs STANDALONE = policy conflict");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 2, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "f");
	rt_close(&d);
	RT_CHECK(err, "delete f left");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "f", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever f right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R5: GONE vs ANCHOR -- left deletes the path, right hardlinks a
 * second name onto its dnode (a join via the degenerate anchor).
 * DELETE_VS_RELINK; the new name's own row resolves ANCHOR.
 */
static int
test_merge_gone_vs_anchor(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 0, 0, 1, 0, 0, 1 },
		.mme_total = 1,
		.mme_relink = 1,
	};

	TEST_START("R5: GONE vs ANCHOR = DELETE_VS_RELINK");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "f");
	rt_close(&d);
	RT_CHECK(err, "delete f left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "g", obj);
	rt_close(&d);
	RT_CHECK(err, "link g right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R6: divergent joins -- left relinks f into P1, right relinks f
 * into P2. DIVERGENT_MEMBERSHIP; both pools' own members are
 * silent.
 */
static int
test_merge_divergent_joins(void)
{
	static const char *const p1[] = { "a", "b" };
	static const char *const p2[] = { "c", "d" };
	rt_ds_t d;
	uint64_t d1, d2;
	int err;
	mm_expect_t e = {
		.mme_final = { 4, 0, 0, 0, 0, 0, 1 },
		.mme_total = 1,
		.mme_divergent = 1,
	};

	TEST_START("R6: divergent joins = DIVERGENT_MEMBERSHIP");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(p1, 2, "x", 1, &d1), "base P1");
	RT_CHECK(mm_base_pool(p2, 2, "y", 1, &d2), "base P2");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "z", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate f");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "f"));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "f", d1);
	rt_close(&d);
	RT_CHECK(err, "left join P1");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "f"));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "f", d2);
	rt_close(&d);
	RT_CHECK(err, "right join P2");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R7: FRAGMENT vs ANCHOR(another pool) -- left severed c into a
 * fragment of P, right relinked c into Q. DIVERGENT_MEMBERSHIP,
 * and the conflicted row keeps P alive in the dead-pool sweep.
 */
static int
test_merge_fragment_vs_anchor(void)
{
	static const char *const p[] = { "a", "b", "c", "d" };
	static const char *const q[] = { "x1", "x2" };
	static const char *const lsev[] = { "c", "d" };
	rt_ds_t d;
	uint64_t qobj;
	int err;
	mm_expect_t e = {
		.mme_final = { 4, 0, 0, 0, 1, 0, 1 },
		.mme_total = 1,
		.mme_divergent = 1,
	};

	TEST_START("R7: FRAGMENT vs ANCHOR = divergent");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(p, 4, "x", 1, NULL), "base P");
	RT_CHECK(mm_base_pool(q, 2, "q", 1, &qobj), "base Q");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_sever_set(RT_DS_LEFT, lsev, 2, "x", 1, NULL),
	    "left sever");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "c"));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "c", qobj);
	rt_close(&d);
	RT_CHECK(err, "right relink c");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R8: unlink is not delete -- until membership hits zero. Left
 * unlinks every member; right edited the content believing the
 * pool lived. ONE LINKPOOL_CONTENT record fires, member list as
 * alt paths; the per-path rows themselves resolve GONE cleanly.
 */
static int
test_merge_dead_pool_edited(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	uint64_t obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 3, 0, 0, 0, 0, 0 },
		.mme_total = 1,
		.mme_content = 1,
	};

	TEST_START("R8: dead pool + edit = one content conflict");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, &obj), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "A"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "B"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	rt_close(&d);
	RT_CHECK(err, "unlink all left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R9: dead pool with a silent other side is a clean deletion --
 * the control for R8.
 */
static int
test_merge_dead_pool_silent(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 3, 0, 0, 0, 0, 0 },
	};

	TEST_START("R9: dead pool, silent side, no conflict");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "A"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "B"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	rt_close(&d);
	RT_CHECK(err, "unlink all left");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R10: a conflicted member keeps the pool alive. Left unlinks all
 * three; right severs A (GONE-vs-STANDALONE relink conflict) and
 * edits the survivors. Only the relink conflict fires --
 * LINKPOOL_CONTENT must not compound onto the undecided pool.
 */
static int
test_merge_conflicted_keeps_alive(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	uint64_t obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 2, 0, 0, 0, 0, 1 },
		.mme_total = 1,
		.mme_relink = 1,
	};

	TEST_START("R10: conflicted member keeps pool alive");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, &obj), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "A"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "B"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	rt_close(&d);
	RT_CHECK(err, "unlink all left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "x",
	    1, NULL));
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "sever + edit right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R11: joins keep the pool alive. Left churns every base name off
 * the dnode while linking new ones (collapse turns the churn into
 * member moves; the targets are identical either way); right
 * edits the content. The lineage lives on through the joined
 * paths, so no content conflict fires.
 */
static int
test_merge_churn_alive(void)
{
	static const char *const names[] = { "f", "g" };
	rt_ds_t d;
	uint64_t obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 0, 2, 0, 2, 0, 0, 0 },
	};

	TEST_START("R11: joins keep the pool alive");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 2, "x", 1, &obj), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "x", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "y", obj));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "f"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "g");
	rt_close(&d);
	RT_CHECK(err, "churn left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R12: sever and content edit are orthogonal intents, both
 * honored: A goes standalone on left's word, right's edit applies
 * to the surviving pool, and nothing conflicts. (The
 * LINKPOOL_SHRUNK warning is phase F's business, not a conflict.)
 */
static int
test_merge_sever_vs_edit(void)
{
	static const char *const names[] = { "A", "B", "C" };
	rt_ds_t d;
	uint64_t obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 0, 1, 0, 0, 0, 0 },
	};

	TEST_START("R12: sever vs edit, orthogonal, no conflict");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(names, 3, "x", 1, &obj), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever A left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R13: GONE vs FRAGMENT -- left severed {c,d} into a fragment,
 * right deleted c. DELETE_VS_RELINK keyed by the fragment id; the
 * unconflicted fragment member still resolves FRAGMENT.
 */
static int
test_merge_gone_vs_fragment(void)
{
	static const char *const p[] = { "a", "b", "c", "d" };
	static const char *const lsev[] = { "c", "d" };
	rt_ds_t d;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 0, 0, 0, 1, 0, 1 },
		.mme_total = 1,
		.mme_relink = 1,
	};

	TEST_START("R13: GONE vs FRAGMENT = DELETE_VS_RELINK");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(p, 4, "x", 1, NULL), "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(mm_sever_set(RT_DS_LEFT, lsev, 2, "x", 1, NULL),
	    "left sever");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "c");
	rt_close(&d);
	RT_CHECK(err, "delete c right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R15: a run with no linkpools anywhere resolves trivially -- an
 * edit row lands in the same bucket, a delete row in gone, and
 * the row-sum invariant holds with the conflict machinery idle.
 */
static int
test_merge_standalone_trivial(void)
{
	rt_ds_t d;
	uint64_t subdir, obj;
	int err;
	mm_expect_t e = {
		.mme_final = { 1, 1, 0, 0, 0, 0, 0 },
	};

	TEST_START("R15: standalone rows resolve trivially");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_edit_file(d.rtd_os, obj, "edited!", 7));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir",
	    &subdir));
	err = rt_remove_entry(d.rtd_os, subdir, "inner");
	rt_close(&d);
	RT_CHECK(err, "edit + delete");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * R16: manifest dedup -- left deletes two members of P, right
 * relinks both into Q. Two conflicted rows, ONE DELETE_VS_RELINK
 * record (same destination, same type), the second path in
 * rcf_alt_paths.
 */
static int
test_merge_dedup_alt_paths(void)
{
	static const char *const p[] = { "a", "b", "c" };
	static const char *const q[] = { "x1", "x2" };
	rt_ds_t d;
	uint64_t qobj;
	int err;
	mm_expect_t e = {
		.mme_final = { 2, 0, 1, 0, 0, 0, 2 },
		.mme_total = 1,
		.mme_relink = 1,
	};

	TEST_START("R16: same-type conflicts dedup to one record");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(mm_base_pool(p, 3, "x", 1, NULL), "base P");
	RT_CHECK(mm_base_pool(q, 2, "q", 1, &qobj), "base Q");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "b"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "c");
	rt_close(&d);
	RT_CHECK(err, "delete b,c left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "b"));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "b", qobj));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "c"));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "c", qobj);
	rt_close(&d);
	RT_CHECK(err, "relink b,c right");

	if (mm_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

void
run_merge_tests(void)
{
	(void) printf("\n[merge: crossref phases C+D (U/R matrix)]\n");
	(void) test_merge_fragment_overlap();
	(void) test_merge_fragment_disjoint();
	(void) test_merge_fragment_vs_novel();
	(void) test_merge_novel_unified();
	(void) test_merge_novel_overlap_conflict();
	(void) test_merge_novel_disjoint();
	(void) test_merge_novel_chain();
	(void) test_merge_recycled_unified();
	(void) test_merge_data_fault();
	(void) test_merge_sever_acceptance();
	(void) test_merge_sever_agreement();
	(void) test_merge_lone_gone();
	(void) test_merge_gone_vs_standalone();
	(void) test_merge_gone_vs_anchor();
	(void) test_merge_divergent_joins();
	(void) test_merge_fragment_vs_anchor();
	(void) test_merge_dead_pool_edited();
	(void) test_merge_dead_pool_silent();
	(void) test_merge_conflicted_keeps_alive();
	(void) test_merge_churn_alive();
	(void) test_merge_sever_vs_edit();
	(void) test_merge_gone_vs_fragment();
	(void) test_merge_standalone_trivial();
	(void) test_merge_dedup_alt_paths();
}
