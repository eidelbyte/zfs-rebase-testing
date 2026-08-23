// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Linkpool-anchor matrix (A classification/rescue, T membership
 * targets) tests -- see TEST-MATRIX.md, "Linkpool-anchor matrix".
 * Each test's comment names the cell it covers.
 *
 * Cross-reference phases A and B are observed through the four
 * per-branch summary lines via rt_anchor_stats() and
 * rt_target_stats(). Every test asserts a 27-value expectation:
 * linkpool-member paths (the walk counter that validates a pool
 * fixture), both changelist counts, the four move counters, the
 * eight anchor tallies, and the twelve target tallies. The visited
 * and hysteria counters are deliberately not asserted here (see
 * the matrix preamble). Each side's target tallies sum to the row
 * count, so the two sums are checked against each other on every
 * test (cell T8).
 */

#include "rebase_test.h"

/*
 * Expected 27-tuple for one A/T fixture. Anchor tally order is
 * (anchored, novel, recycled, fragment); target tally order is
 * (same, gone, standalone, anchor, fragment, novel). Designated
 * initializers keep unexercised counters at zero.
 */
typedef struct ab_expect {
	uint64_t	abe_linked;
	uint64_t	abe_cl, abe_cr;
	uint64_t	abe_ml, abe_mr, abe_mel, abe_mer;
	uint64_t	abe_an_l[4], abe_an_r[4];
	uint64_t	abe_tg_l[6], abe_tg_r[6];
} ab_expect_t;

static void
ab_print(const char *tag, uint64_t lk, uint64_t cl, uint64_t cr,
    const rt_move_stats_t *ms, const uint64_t an_l[4],
    const uint64_t an_r[4], const uint64_t tg_l[6],
    const uint64_t tg_r[6])
{
	(void) printf("    [%s] lk=%llu cl=%llu cr=%llu "
	    "mv=%llu/%llu me=%llu/%llu\n"
	    "        an L(%llu,%llu,%llu,%llu) "
	    "R(%llu,%llu,%llu,%llu)\n"
	    "        tg L(%llu,%llu,%llu,%llu,%llu,%llu) "
	    "R(%llu,%llu,%llu,%llu,%llu,%llu)\n", tag,
	    (unsigned long long)lk, (unsigned long long)cl, (unsigned long long)cr,
	    (unsigned long long)ms->rms_moves_left,
	    (unsigned long long)ms->rms_moves_right,
	    (unsigned long long)ms->rms_move_edits_left,
	    (unsigned long long)ms->rms_move_edits_right,
	    (unsigned long long)an_l[0], (unsigned long long)an_l[1],
	    (unsigned long long)an_l[2], (unsigned long long)an_l[3],
	    (unsigned long long)an_r[0], (unsigned long long)an_r[1],
	    (unsigned long long)an_r[2], (unsigned long long)an_r[3],
	    (unsigned long long)tg_l[0], (unsigned long long)tg_l[1],
	    (unsigned long long)tg_l[2], (unsigned long long)tg_l[3],
	    (unsigned long long)tg_l[4], (unsigned long long)tg_l[5],
	    (unsigned long long)tg_r[0], (unsigned long long)tg_r[1],
	    (unsigned long long)tg_r[2], (unsigned long long)tg_r[3],
	    (unsigned long long)tg_r[4], (unsigned long long)tg_r[5]);
}

/*
 * Sync, run the rebase (expecting success), scrape all five
 * summary lines, tear the pool down, and compare the 27-tuple.
 * Also checks the structural row-count invariant (T8): each side's
 * target tallies sum to the number of membership rows, so the two
 * sums must agree.
 */
static int
ab_finish(const ab_expect_t *e)
{
	nvlist_t *nvl;
	rt_walk_stats_t ws;
	rt_move_stats_t ms, ems;
	rt_anchor_stats_t as;
	rt_target_stats_t ts;
	uint64_t cl, cr, suml = 0, sumr = 0;
	boolean_t bad = B_FALSE;
	int err, i;

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err != 0) {
		rt_scaffold_teardown();
		(void) printf("\n    [ab] rebase failed: %s (%d)\n",
		    strerror(err), err);
		return (1);
	}
	fnvlist_free(nvl);

	err = rt_walk_stats(&ws);
	if (err == 0)
		err = rt_changelist_counts(&cl, &cr);
	if (err == 0)
		err = rt_move_stats(&ms);
	if (err == 0)
		err = rt_anchor_stats(&as);
	if (err == 0)
		err = rt_target_stats(&ts);
	rt_scaffold_teardown();
	if (err != 0) {
		(void) printf("\n    [ab] no summary lines (%d)\n",
		    err);
		return (1);
	}

	for (i = 0; i < 6; i++) {
		suml += ts.rts_left[i];
		sumr += ts.rts_right[i];
	}
	if (suml != sumr) {
		(void) printf("\n    [ab] row-count invariant "
		    "broken: left sum %llu != right sum %llu\n",
		    (unsigned long long)suml, (unsigned long long)sumr);
		return (1);
	}

	if (ws.rws_linked != e->abe_linked ||
	    cl != e->abe_cl || cr != e->abe_cr ||
	    ms.rms_moves_left != e->abe_ml ||
	    ms.rms_moves_right != e->abe_mr ||
	    ms.rms_move_edits_left != e->abe_mel ||
	    ms.rms_move_edits_right != e->abe_mer)
		bad = B_TRUE;
	for (i = 0; i < 4; i++) {
		if (as.ras_left[i] != e->abe_an_l[i] ||
		    as.ras_right[i] != e->abe_an_r[i])
			bad = B_TRUE;
	}
	for (i = 0; i < 6; i++) {
		if (ts.rts_left[i] != e->abe_tg_l[i] ||
		    ts.rts_right[i] != e->abe_tg_r[i])
			bad = B_TRUE;
	}

	if (bad) {
		ems.rms_moves_left = e->abe_ml;
		ems.rms_moves_right = e->abe_mr;
		ems.rms_move_edits_left = e->abe_mel;
		ems.rms_move_edits_right = e->abe_mer;
		(void) printf("\n");
		ab_print("expected", e->abe_linked, e->abe_cl,
		    e->abe_cr, &ems, e->abe_an_l, e->abe_an_r,
		    e->abe_tg_l, e->abe_tg_r);
		ab_print("got", ws.rws_linked, cl, cr, &ms,
		    as.ras_left, as.ras_right, ts.rts_left,
		    ts.rts_right);
		return (1);
	}
	return (0);
}

/*
 * Build a base with one linkpool: file "f" (content "x") plus
 * hardlink "g", snapshotted and cloned. Returns the shared object
 * number through objp.
 */
static int
ab_scaffold_pool(uint64_t *objp)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	err = rt_scaffold_empty_base();
	if (err != 0)
		return (err);

	err = rt_open(RT_DS_SRC, &d);
	if (err != 0)
		return (err);
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "g",
		    obj);
	rt_close(&d);
	if (err != 0)
		return (err);
	if (objp != NULL)
		*objp = obj;

	return (rt_scaffold_snap_and_clone());
}

/*
 * A1: an untouched base linkpool classifies ANCHORED on both sides
 * via the fork-txg fast path; its member rows are SAME/SAME.
 */
static int
test_anchor_untouched_pool(void)
{
	uint64_t obj;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 2, 0, 0, 0, 0, 0 },
		.abe_tg_r = { 2, 0, 0, 0, 0, 0 },
	};

	TEST_START("A1: untouched pool anchors (fast path)");
	RT_CHECK(ab_scaffold_pool(&obj), "scaffold failed");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A2: an in-place content edit dirties the shared dnode, so the
 * fast path fails and ANCHORED comes from the ZPL_GEN compare.
 * Both member paths carry EDIT records; membership is unchanged,
 * so both targets stay SAME_AS_BASE.
 */
static int
test_anchor_edited_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 2,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 2, 0, 0, 0, 0, 0 },
		.abe_tg_r = { 2, 0, 0, 0, 0, 0 },
	};

	TEST_START("A2: edited pool anchors via gen");
	RT_CHECK(ab_scaffold_pool(&obj), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "edit pool dnode");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A3: a post-fork pool (create plus link on the left) is NOVEL;
 * its members target NOVEL.
 */
static int
test_anchor_novel_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 2,
		.abe_an_l = { 0, 1, 0, 0 },
		.abe_tg_l = { 0, 0, 0, 0, 0, 2 },
		.abe_tg_r = { 2, 0, 0, 0, 0, 0 },
	};

	TEST_START("A3: post-fork pool is NOVEL");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "x", "n", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "y",
		    obj);
	rt_close(&d);
	RT_CHECK(err, "create novel pool");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A4 (the mandatory recycle cell): a gen flip on the pool's dnode
 * classifies RECYCLED, never ANCHORED. Members carry EDIT x
 * MOVED(from == to) records, which also exercises the rescue's
 * parent == own-object guard: no false SPLIT_FRAGMENT. Members
 * target NOVEL.
 */
static int
test_anchor_recycled_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 2,
		.abe_an_l = { 0, 0, 1, 0 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 0, 0, 0, 0, 0, 2 },
		.abe_tg_r = { 2, 0, 0, 0, 0, 0 },
	};

	TEST_START("A4: recycled index is RECYCLED, not ANCHORED");
	RT_CHECK(ab_scaffold_pool(&obj), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_set_sa_u64(d.rtd_os, obj, ZPL_GEN, 424242);
	rt_close(&d);
	RT_CHECK(err, "flip gen");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A5: a pool grown from a base-standalone dnode anchors to the
 * LINEAGE (base's table never listed it). Both member paths
 * expressed a join, so both target ANCHOR.
 */
static int
test_anchor_degenerate_standalone(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 2,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_tg_l = { 0, 0, 0, 2, 0, 0 },
		.abe_tg_r = { 2, 0, 0, 0, 0, 0 },
	};

	TEST_START("A5: base-standalone dnode anchors by lineage");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "g", obj);
	rt_close(&d);
	RT_CHECK(err, "grow pool from standalone");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * Build a base with a three-link pool ("a", "b", "c" on one dnode,
 * content "x"), then sever "b" and "c" on the left into one new
 * shared dnode with identical content. Returns the fragment's
 * object number through zp for follow-on fixtures.
 */
static int
ab_fragment_fixture(uint64_t *zp)
{
	rt_ds_t d;
	uint64_t obj, z;
	int err;

	err = rt_scaffold_empty_base();
	if (err != 0)
		return (err);

	err = rt_open(RT_DS_SRC, &d);
	if (err != 0)
		return (err);
	err = rt_create_file(d.rtd_os, d.rtd_root, "a", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "b",
		    obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "c",
		    obj);
	rt_close(&d);
	if (err != 0)
		return (err);
	err = rt_scaffold_snap_and_clone();
	if (err != 0)
		return (err);

	err = rt_open(RT_DS_LEFT, &d);
	if (err != 0)
		return (err);
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "b");
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "c");
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "b",
		    "x", 1, &z);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "c", z);
	rt_close(&d);
	if (err == 0 && zp != NULL)
		*zp = z;
	return (err);
}

/*
 * A6: a clean split fragment -- two members severed to one new
 * dnode, MOVED provenance from exactly one parent -- is rescued to
 * SPLIT_FRAGMENT. Its members target FRAGMENT; the abandoned mate
 * targets STANDALONE (its pool dissolved to one link).
 */
static int
test_anchor_fragment(void)
{
	ab_expect_t e = {
		.abe_linked = 3,
		.abe_cl = 3,
		.abe_an_l = { 0, 0, 0, 1 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 0, 0, 1, 0, 2, 0 },
		.abe_tg_r = { 3, 0, 0, 0, 0, 0 },
	};

	TEST_START("A6: split fragment rescued with parentage");
	RT_CHECK(ab_fragment_fixture(NULL), "fixture failed");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A7: a path hardlinked into the fragment after the split (an
 * ADDED member) is neutral: the rescue still fires, and the
 * newcomer targets FRAGMENT alongside the severed members.
 */
static int
test_anchor_fragment_added_member(void)
{
	rt_ds_t d;
	uint64_t z;
	int err;
	ab_expect_t e = {
		.abe_linked = 4,
		.abe_cl = 4,
		.abe_an_l = { 0, 0, 0, 1 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 0, 0, 1, 0, 3, 0 },
		.abe_tg_r = { 4, 0, 0, 0, 0, 0 },
	};

	TEST_START("A7: ADDED member keeps the fragment rescue");
	RT_CHECK(ab_fragment_fixture(&z), "fixture failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "w", z);
	rt_close(&d);
	RT_CHECK(err, "link post-split member");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A8: a new pool drawing severed members from TWO base pools has
 * two distinct moved-from parents: the rescue declines and the
 * pool stays NOVEL (a merge of severed halves is not one
 * fragment). Both abandoned mates go STANDALONE.
 */
static int
test_anchor_two_parents(void)
{
	rt_ds_t d;
	uint64_t d1, d2, z;
	int err;
	ab_expect_t e = {
		.abe_linked = 4,
		.abe_cl = 4,
		.abe_an_l = { 0, 1, 0, 0 },
		.abe_an_r = { 2, 0, 0, 0 },
		.abe_tg_l = { 0, 0, 2, 0, 0, 2 },
		.abe_tg_r = { 4, 0, 0, 0, 0, 0 },
	};

	TEST_START("A8: two parents disqualify the rescue");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "a", "x", 1,
	    &d1);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "b",
		    d1);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "c",
		    "y", 1, &d2);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "e",
		    d2);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "b"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "c"));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "b",
	    "merged!", 7, &z));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "c", z);
	rt_close(&d);
	RT_CHECK(err, "merge severed halves");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A9: ZPL_GEN unreadable when phase A classifies the pool is EIO.
 * The fixture repoints the dnode's only base path onto a fresh
 * dnode (an EDIT between different objects -- no gen read) and
 * builds the pool from new paths only (ADDs -- no gen read), with
 * no ADD+DELETE run on the object (move-collapse never runs its
 * gen gate): the classify step is genuinely the first gen reader.
 */
static int
test_anchor_gen_missing(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("A9: missing gen at classify = EIO");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "x", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "y", obj));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "f"));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "f",
	    "changed!", 8, NULL));
	err = rt_remove_sa_attr(d.rtd_os, obj, ZPL_GEN);
	rt_close(&d);
	RT_CHECK(err, "remove gen");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EIO, "expected EIO");
	TEST_PASS();
}

/*
 * A10 (the doc-correction witness): every base path of the dnode
 * turned over -- new links added, the only original name deleted,
 * nlink never zero. The old survivors scan would falsely RECYCLE
 * this; ZPL_GEN proves the lineage held, so the pool ANCHORS. The
 * deleted name is GONE, the new names target ANCHOR (the guard
 * shape is M7's, so nothing collapses).
 */
static int
test_anchor_link_churn(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 3,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_tg_l = { 0, 1, 0, 2, 0, 0 },
		.abe_tg_r = { 3, 0, 0, 0, 0, 0 },
	};

	TEST_START("A10: link churn keeps the anchor (gen holds)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "x", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "y", obj));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "f");
	rt_close(&d);
	RT_CHECK(err, "churn links");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A11: several pools on one side tally independently -- an
 * untouched anchored pool and a post-fork novel pool coexist on
 * the left.
 */
static int
test_anchor_multi_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 4,
		.abe_cl = 2,
		.abe_an_l = { 1, 1, 0, 0 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 2, 0, 0, 0, 0, 2 },
		.abe_tg_r = { 4, 0, 0, 0, 0, 0 },
	};

	TEST_START("A11: anchored and novel pools tally per side");
	RT_CHECK(ab_scaffold_pool(&obj), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "x", "n", 1,
	    &obj));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "y", obj);
	rt_close(&d);
	RT_CHECK(err, "create novel pool");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * A12: side symmetry -- a novel pool on the RIGHT lands in the
 * right-hand tallies and right-hand NOVEL targets; the left
 * column is silence (SAME_AS_BASE).
 */
static int
test_anchor_right_novel(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cr = 2,
		.abe_an_r = { 0, 1, 0, 0 },
		.abe_tg_l = { 2, 0, 0, 0, 0, 0 },
		.abe_tg_r = { 0, 0, 0, 0, 0, 2 },
	};

	TEST_START("A12: novel pool on the right");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "x", "n", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "y",
		    obj);
	rt_close(&d);
	RT_CHECK(err, "create novel pool right");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * T1: a pooled member's DELETE record maps to GONE; the surviving
 * members express nothing (their pool still anchors, their base
 * membership is unchanged) and stay SAME_AS_BASE.
 */
static int
test_target_gone_delete(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 3,
		.abe_cl = 1,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 2, 1, 0, 0, 0, 0 },
		.abe_tg_r = { 3, 0, 0, 0, 0, 0 },
	};

	TEST_START("T1: pooled DELETE targets GONE");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B",
		    obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "C",
		    obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	rt_close(&d);
	RT_CHECK(err, "unlink C");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * T2: a collapsed member move leaves no record and no table entry
 * at its old path, but the old path's row is synthesized GONE from
 * rc_old_path. The new path joined the (still anchored, untouched)
 * pool, so it targets ANCHOR; the mate stays SAME.
 */
static int
test_target_gone_move_source(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;
	ab_expect_t e = {
		.abe_linked = 3,
		.abe_cl = 1,
		.abe_ml = 1,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 1, 1, 0, 1, 0, 0 },
		.abe_tg_r = { 3, 0, 0, 0, 0, 0 },
	};

	TEST_START("T2: member-move old path synthesized GONE");
	RT_CHECK(ab_scaffold_pool(&obj), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "f",
	    d.rtd_root, "f2");
	rt_close(&d);
	RT_CHECK(err, "rename member");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * T3: a sever (hysterical standalone copy) dissolves the two-link
 * pool: the severed path and the abandoned mate both target
 * STANDALONE, and the left side has no pools at all.
 */
static int
test_target_severed_standalone(void)
{
	rt_ds_t d;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 2,
		.abe_an_r = { 1, 0, 0, 0 },
		.abe_tg_l = { 0, 0, 2, 0, 0, 0 },
		.abe_tg_r = { 2, 0, 0, 0, 0, 0 },
	};

	TEST_START("T3: sever targets STANDALONE for both paths");
	RT_CHECK(ab_scaffold_pool(NULL), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "f", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever f");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * T5: relinking a member path into another anchored pool targets
 * ANCHOR(the new pool's lineage) -- the base table shows a
 * different lineage, so the change is expressed. The abandoned
 * mate goes STANDALONE; the target pool's own members express
 * nothing.
 */
static int
test_target_relink_anchor(void)
{
	rt_ds_t d;
	uint64_t d1, d2;
	int err;
	ab_expect_t e = {
		.abe_linked = 4,
		.abe_cl = 2,
		.abe_an_l = { 1, 0, 0, 0 },
		.abe_an_r = { 2, 0, 0, 0 },
		.abe_tg_l = { 2, 0, 1, 1, 0, 0 },
		.abe_tg_r = { 4, 0, 0, 0, 0, 0 },
	};

	TEST_START("T5: relink targets ANCHOR(new pool)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "a", "x", 1,
	    &d1);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "b",
		    d1);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "c",
		    "y", 1, &d2);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "e",
		    d2);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "b"));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "b", d2);
	rt_close(&d);
	RT_CHECK(err, "relink b");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * T9: one row, two expressed opinions -- left deletes the path
 * (GONE), right severs it (STANDALONE). Both sides dissolve the
 * pool, so the mate targets STANDALONE on both and no pools
 * survive anywhere.
 */
static int
test_target_both_sides(void)
{
	rt_ds_t d;
	int err;
	ab_expect_t e = {
		.abe_linked = 2,
		.abe_cl = 2,
		.abe_cr = 2,
		.abe_tg_l = { 0, 1, 1, 0, 0, 0 },
		.abe_tg_r = { 0, 0, 2, 0, 0, 0 },
	};

	TEST_START("T9: dual-sided row, GONE vs STANDALONE");
	RT_CHECK(ab_scaffold_pool(NULL), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "f");
	rt_close(&d);
	RT_CHECK(err, "delete f left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "f", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "sever f right");

	if (ab_finish(&e))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

void
run_anchor_tests(void)
{
	(void) printf("\n[anchor: crossref phases A+B (A/T matrix)]\n");
	(void) test_anchor_untouched_pool();
	(void) test_anchor_edited_pool();
	(void) test_anchor_novel_pool();
	(void) test_anchor_recycled_pool();
	(void) test_anchor_degenerate_standalone();
	(void) test_anchor_fragment();
	(void) test_anchor_fragment_added_member();
	(void) test_anchor_two_parents();
	(void) test_anchor_gen_missing();
	(void) test_anchor_link_churn();
	(void) test_anchor_multi_pool();
	(void) test_anchor_right_novel();
	(void) test_target_gone_delete();
	(void) test_target_gone_move_source();
	(void) test_target_severed_standalone();
	(void) test_target_relink_anchor();
	(void) test_target_both_sides();
}
