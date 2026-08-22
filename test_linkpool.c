// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Linkpool cases. A linkpool is the set of paths all referencing
 * the same dnode through hardlinks; in the sprint-2 two-axis model
 * these operations produce linkpool-axis records independent of the
 * content axis.
 *
 * Two eras of tests live here. The walk-era matrix cells (LP rows
 * for discovery and membership, LV rows for the completeness
 * verify; see TEST-MATRIX.md) assert only return codes and are
 * meaningful from zap-walk-basic onward. The older manifest-inspecting tests below
 * them assert crossref-era behavior and fail (cleanly, via the
 * defensive accessors) until the emit issues land. The sprint-2
 * 14-test catalog (sever-vs-nothing, phantom-conflict dissolution,
 * index recycling, novel overlap, ...) also lands in this file with
 * the testing-framework issue.
 *
 * NOTE on LV cells: against a DEBUG libzpool the completeness
 * mismatch ASSERTs (panics) instead of returning EIO by design; the
 * LV tests expect the production behavior of the non-debug FreeBSD
 * system library.
 */

#include "rebase_test.h"

/*
 * LP1/LP3/LP4/LP5 + LP2: linkpool discovery matrix, one fixture.
 *
 *   pair2: p2a + p2b          two links, one directory (LP1, LP5)
 *   trio:  t1, s/t2, s/ss/t3  three links across three directory
 *                             levels (LP2: cross-dir completeness)
 *   quad:  q1 + q2            second pool, discovery interleaved
 *                             with the others (LP3)
 *   nm:    s/nm + s/ss/nm     same leaf name in different dirs,
 *                             one dnode (LP4: by-path distinctness)
 *
 * No side changes anything: three identical tables must build and
 * pass the completeness verify on all three branches.
 */
static int
test_linkpool_discovery_matrix(void)
{
	rt_ds_t d;
	uint64_t s, ss, obj;
	int err;

	TEST_START("linkpool: discovery matrix");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p2a",
	    "pp\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "p2b", obj));

	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "s", &s));
	VERIFY0(rt_create_dir(d.rtd_os, s, "ss", &ss));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "t1",
	    "tt\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, s, "t2", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, ss, "t3", obj));

	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "q1",
	    "qq\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "q2", obj));

	VERIFY0(rt_create_file(d.rtd_os, s, "nm", "nn\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, ss, "nm", obj));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * LP6/LP7/LP8: membership operations with real nlink maintenance.
 * Base pool {m1, m2, m3}: left links m4 (3 -> 4), right unlinks m3
 * (3 -> 2). Base pool {d1, d2}: left unlinks both (2 -> 0, the
 * dnode lingers pathless like an orphan). Every table must still
 * pass its completeness verify.
 */
static int
test_linkpool_membership_ops(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("linkpool: membership ops (link/unlink/dissolve)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "m1",
	    "m\n", 2, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "m2", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "m3", obj));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "d1",
	    "dd\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "d2", obj));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "m1", &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "m4", obj));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "d1"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "d2"));
	rt_close(&d);

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "m3"));
	rt_close(&d);

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Shared fixture for the LV corruption cells: a two-link pool
 * {h, hl} in base, cloned everywhere.
 */
static int
lv_scaffold(void)
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
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "h",
	    "hh\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hl", obj));
	rt_close(&d);
	return (rt_scaffold_snap_and_clone());
}

/*
 * Corrupt nlink on one branch's copy of "h" and expect the
 * completeness verify to abort the rebase with EIO.
 */
static int
lv_corrupt_and_expect_eio(const char *dsname, uint64_t nlink)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	RT_CHECK(rt_open(dsname, &d), "hold branch");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "h", &obj));
	err = rt_set_nlink(d.rtd_os, obj, nlink);
	rt_close(&d);
	RT_CHECK(err, "set nlink");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EIO, "expected EIO (incomplete linkpool)");
	TEST_PASS();
}

/*
 * LV1: nlink overstates the entries on the LEFT branch. This cell
 * is also the positive proof that linkpool discovery ran at all: a
 * walker that never read ZPL_LINKS could not fail here.
 */
static int
test_linkpool_nlink_over_left(void)
{
	TEST_START("linkpool: nlink > entries on left -> EIO");
	RT_CHECK(lv_scaffold(), "scaffold failed");
	return (lv_corrupt_and_expect_eio(RT_DS_LEFT, 5));
}

/*
 * LV2: corruption in the BASE table. The base is an immutable
 * snapshot, so the bad nlink is injected in src BEFORE the
 * snapshot is taken; all three branches inherit it and the first
 * table verified (base) trips.
 */
static int
test_linkpool_nlink_over_base(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("linkpool: nlink > entries in base -> EIO");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "h",
	    "hh\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hl", obj));
	err = rt_set_nlink(d.rtd_os, obj, 5);
	rt_close(&d);
	RT_CHECK(err, "set nlink");

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EIO, "expected EIO (incomplete linkpool)");
	TEST_PASS();
}

/*
 * LV3: nlink overstates the entries on the RIGHT branch.
 */
static int
test_linkpool_nlink_over_right(void)
{
	TEST_START("linkpool: nlink > entries on right -> EIO");
	RT_CHECK(lv_scaffold(), "scaffold failed");
	return (lv_corrupt_and_expect_eio(RT_DS_RIGHT, 5));
}

/*
 * LV4: nlink UNDERSTATES the entries -- three links, nlink forced
 * to 2. Discovery still triggers (2 > 1) and the walk finds three
 * members: rlp_nfound > rlp_nlink must abort just like the
 * overstatement cells. (LV5, nlink == 1 with two entries, is NOT
 * detectable by design -- discovery keys on nlink > 1 -- and is
 * documented as such in TEST-MATRIX.md.)
 */
static int
test_linkpool_nlink_under(void)
{
	rt_ds_t d;
	uint64_t obj;

	TEST_START("linkpool: nlink < entries on left -> EIO");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "h",
	    "hh\n", 3, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hl", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hl2", obj));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");
	return (lv_corrupt_and_expect_eio(RT_DS_LEFT, 2));
}

/*
 * Left adds a second dir entry for an existing dnode. In two-axis
 * terms: a content no-op with a linkpool ADDED at the new path.
 */
static int
test_left_hardlink(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("left adds a hardlink");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "hello_link", obj);
	rt_close(&d);
	RT_CHECK(err, "add hardlink");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Base has "hello" and "hello_link" sharing one dnode. Left edits
 * through one path, right edits through the other with different
 * content: ONE conflict on the shared dnode, with the second path
 * as an alt path -- never two conflicts.
 */
static int
test_edge_hardlink_edit_both_sides(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: hardlink edit both sides (dedup)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	/* Populate src: file "hello" + hardlink "hello_link". */
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "original\n", 9, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hello_link",
	    obj));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	/* Left edits the dnode (via hello). */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "left-edit\n", 10);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	/* Right edits the same dnode (via hello_link, same obj). */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello_link",
	    &obj));
	err = rt_edit_file(d.rtd_os, obj, "right-edit\n", 11);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict (hardlink dedup)");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello") ||
	    rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello_link"),
	    "expected BOTH_MODIFIED at hello or hello_link");
	TEST_EXPECT(rt_manifest_conflict_nalt(nvl, "hello") == 1 ||
	    rt_manifest_conflict_nalt(nvl, "hello_link") == 1,
	    "expected 1 alt_path (hardlink dedup)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left removes one link of a two-link file; the other path and the
 * dnode survive. A membership-only change (in two-axis terms:
 * REBASE_CONTENT_NONE plus REBASE_LINKPOOL_REMOVED), never a
 * conflict.
 */
static int
test_edge_hardlink_delete_no_conflict(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: hardlink delete, no conflict");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "content\n", 8, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hello_link",
	    obj));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	/* Left removes the hardlink (original "hello" remains). */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello_link");
	rt_close(&d);
	RT_CHECK(err, "remove hardlink");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (hardlink delete, no conflict)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left removes one link, right edits the shared dnode through that
 * same link path: the link removal collides with the edit at the
 * path level.
 */
static int
test_edge_hardlink_delete_vs_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: hardlink delete vs edit same path");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "content\n", 8, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "hello_link",
	    obj));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	/* Left removes the hardlink. */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello_link");
	rt_close(&d);
	RT_CHECK(err, "remove hardlink");

	/* Right edits the dnode (visible at both paths). */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello_link",
	    &obj));
	err = rt_edit_file(d.rtd_os, obj, "right-edit\n", 11);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(
	    rt_manifest_has_conflict(nvl, "DELETE_MODIFY",
	    "hello_link") ||
	    rt_manifest_has_conflict(nvl, "MODIFY_DELETE",
	    "hello_link"),
	    "expected DELETE/MODIFY conflict at hello_link");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_linkpool_tests(void)
{
	(void) printf("\n[linkpool: discovery, membership, verify]\n");
	(void) test_linkpool_discovery_matrix();
	(void) test_linkpool_membership_ops();
	(void) test_linkpool_nlink_over_left();
	(void) test_linkpool_nlink_over_base();
	(void) test_linkpool_nlink_over_right();
	(void) test_linkpool_nlink_under();
	(void) test_left_hardlink();
	(void) test_edge_hardlink_edit_both_sides();
	(void) test_edge_hardlink_delete_no_conflict();
	(void) test_edge_hardlink_delete_vs_edit();
}
