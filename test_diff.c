// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Standalone-diff matrix (D) tests -- see TEST-MATRIX.md,
 * "Standalone-diff matrix". Each test's comment names the cell(s)
 * it covers.
 *
 * Records are observed through the walk's second dbgmsg line via
 * rt_changelist_counts(); every test asserts the full six-tuple
 * (visited, hysterical left/right, linkpool-member paths,
 * changelist left/right) so no counter can drift unnoticed.
 * Fixtures edit the LEFT side unless the test says otherwise; D17
 * pins right-side symmetry. Side effects are part of the tuples:
 * adding or removing a hardlink touches the mate paths' nlink, so
 * their hysterical and linkpool-only records are counted too.
 */

#include "rebase_test.h"

/*
 * Sync, run the rebase (expecting the ENOSYS success sentinel),
 * scrape all three summary lines, tear the pool down, and compare
 * the full ten-tuple: the six D counters plus the four
 * move-collapse counters. Returns 0 on match; prints the mismatch
 * and returns nonzero otherwise. test_moves.c shares this helper
 * as moves_finish (declared in rebase_test.h).
 */
int
diff_finish_full(uint64_t ev, uint64_t ehl, uint64_t ehr,
    uint64_t elk, uint64_t ecl, uint64_t ecr, uint64_t eml,
    uint64_t emr, uint64_t emel, uint64_t emer)
{
	nvlist_t *nvl;
	rt_walk_stats_t ws;
	rt_move_stats_t ms;
	uint64_t cl, cr;
	int err;

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err != 0) {
		rt_scaffold_teardown();
		(void) printf("\n    [diff] rebase failed: %s (%d)\n",
		    strerror(err), err);
		return (1);
	}
	fnvlist_free(nvl);

	err = rt_walk_stats(&ws);
	if (err == 0)
		err = rt_changelist_counts(&cl, &cr);
	if (err == 0)
		err = rt_move_stats(&ms);
	rt_scaffold_teardown();
	if (err != 0) {
		(void) printf("\n    [diff] no summary lines "
		    "(%d)\n", err);
		return (1);
	}

	if (ws.rws_visited != ev || ws.rws_hyst_left != ehl ||
	    ws.rws_hyst_right != ehr || ws.rws_linked != elk ||
	    cl != ecl || cr != ecr ||
	    ms.rms_moves_left != eml || ms.rms_moves_right != emr ||
	    ms.rms_move_edits_left != emel ||
	    ms.rms_move_edits_right != emer) {
		(void) printf("\n    [diff] expected v=%llu hl=%llu "
		    "hr=%llu lk=%llu cl=%llu cr=%llu\n"
		    "           ml=%llu mr=%llu mel=%llu mer=%llu,\n"
		    "           got v=%llu hl=%llu hr=%llu lk=%llu "
		    "cl=%llu cr=%llu\n"
		    "           ml=%llu mr=%llu mel=%llu mer=%llu\n",
		    (unsigned long long)ev, (unsigned long long)ehl,
		    (unsigned long long)ehr, (unsigned long long)elk,
		    (unsigned long long)ecl, (unsigned long long)ecr,
		    (unsigned long long)eml, (unsigned long long)emr,
		    (unsigned long long)emel,
		    (unsigned long long)emer,
		    (unsigned long long)ws.rws_visited,
		    (unsigned long long)ws.rws_hyst_left,
		    (unsigned long long)ws.rws_hyst_right,
		    (unsigned long long)ws.rws_linked,
		    (unsigned long long)cl, (unsigned long long)cr,
		    (unsigned long long)ms.rms_moves_left,
		    (unsigned long long)ms.rms_moves_right,
		    (unsigned long long)ms.rms_move_edits_left,
		    (unsigned long long)ms.rms_move_edits_right);
		return (1);
	}
	return (0);
}

/*
 * The D-cell shape: six counters, and the moves line asserted all
 * zeros -- no D fixture may produce a spurious collapse (cell M16).
 */
static int
diff_finish(uint64_t ev, uint64_t ehl, uint64_t ehr, uint64_t elk,
    uint64_t ecl, uint64_t ecr)
{
	return (diff_finish_full(ev, ehl, ehr, elk, ecl, ecr,
	    0, 0, 0, 0));
}

/*
 * D1 + D25: untouched tree produces zero records on both sides --
 * the NONE x NONE no-record invariant over every visited path --
 * with H1's hysteria tuple unchanged.
 */
static int
test_diff_untouched(void)
{
	TEST_START("D1: untouched tree, zero records");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");
	if (diff_finish(3, 3, 3, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D2: a plain in-place edit is exactly one record (EDIT x NONE) on
 * the editing side.
 */
static int
test_diff_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D2: in-place edit, one record");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "WORLD\n", 6);
	rt_close(&d);
	RT_CHECK(err, "edit");

	if (diff_finish(3, 2, 3, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D3: a hysterical rename-on-save produces ZERO records -- not one
 * (a leaked EDIT) and not two (a DELETE+ADD split).
 */
static int
test_diff_hysterical_zero(void)
{
	rt_ds_t d;
	int err;

	TEST_START("D3: hysterical edit, zero records");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical edit");

	if (diff_finish(3, 3, 3, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D4: a standalone file added is one record (ADD x NONE).
 */
static int
test_diff_add(void)
{
	rt_ds_t d;
	int err;

	TEST_START("D4: standalone add, one record");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "newfile",
	    "n\n", 2, NULL);
	rt_close(&d);
	RT_CHECK(err, "create");

	if (diff_finish(4, 3, 3, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D5: a standalone file deleted is one record (DELETE x NONE).
 */
static int
test_diff_delete(void)
{
	rt_ds_t d;
	int err;

	TEST_START("D5: standalone delete, one record");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "remove");

	if (diff_finish(3, 2, 3, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D6: a plain rename never survives as a DELETE+ADD split -- the
 * pair collapses to one MOVE record (re-dispositioned 2026-08-23
 * when move-collapse landed, as the original row promised; the
 * collapse mechanics themselves are M1's cell). The changelist
 * count is post-collapse, so cl drops from 2 to 1 and the moves
 * line shows the one pure MOVE.
 */
static int
test_diff_rename_collapses(void)
{
	rt_ds_t d;
	int err;

	TEST_START("D6: rename collapses to one MOVE");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2");
	rt_close(&d);
	RT_CHECK(err, "rename");

	if (diff_finish_full(4, 2, 3, 0, 1, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D7: rename-on-save with NEW content is ONE record -- an EDIT at
 * the path, never a DELETE+ADD split. Content ops are path-scoped.
 */
static int
test_diff_recreate_one_record(void)
{
	rt_ds_t d;
	int err;

	TEST_START("D7: recreate with new content = one EDIT");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "different!\n", 11, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate");

	if (diff_finish(3, 2, 3, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D8: a chmod'd directory produces its own EDIT record.
 */
static int
test_diff_dir_chmod(void)
{
	rt_ds_t d;
	uint64_t dobj;
	int err;

	TEST_START("D8: dir chmod, one record");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "f1", "x", 1,
		    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, dobj, ZPL_MODE,
		    S_IFDIR | 0700);
	rt_close(&d);
	RT_CHECK(err, "chmod dir");

	if (diff_finish(2, 1, 2, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D9: a directory whose only change is its entries contributes NO
 * record of its own -- only the child's ADD exists (the record-
 * level face of the ZPL_SIZE-skip rule).
 */
static int
test_diff_dir_entries(void)
{
	rt_ds_t d;
	uint64_t dobj;
	int err;

	TEST_START("D9: dir entries-only, child record only");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "f1", "x", 1,
		    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "f2", "y", 1,
		    NULL);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, dobj, ZPL_SIZE, 3);
	rt_close(&d);
	RT_CHECK(err, "add child");

	if (diff_finish(3, 2, 2, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D10: sever -- a member path replaced by an identical standalone
 * copy. Two linkpool-only records: the severed path (content NONE,
 * REMOVED via its new object) and the surviving mate (content
 * NONE, REMOVED via the nlink drop). This is the record-level
 * retrospective-2 bug 2 regression, upgrading H31's counter-level
 * assert: hysteria on the content axis must not erase either
 * membership record.
 */
static int
test_diff_sever_identical(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D10: sever to identical copy, two records");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "shared\n",
	    7, &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "B");
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "B",
		    "shared\n", 7, NULL);
	rt_close(&d);
	RT_CHECK(err, "sever B");

	if (diff_finish(2, 2, 2, 2, 2, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D11: join -- a second name hardlinked onto a base standalone
 * file. Two records: the new path (ADD x ADDED) and the old path
 * (linkpool-only NONE x ADDED, since base was not a member).
 */
static int
test_diff_join(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D11: join a standalone file, two records");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "F", "data\n", 5,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "F", &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "F2",
		    obj);
	rt_close(&d);
	RT_CHECK(err, "add link");

	if (diff_finish(2, 1, 1, 2, 2, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D12 + D25: unlink one of three links. Exactly one record (the
 * removed path, DELETE x REMOVED); the two survivors are
 * same-lineage members on both sides and must produce NOTHING --
 * the other half of the no-record invariant.
 */
static int
test_diff_unlink_survivors(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D12: unlink one of three, survivors silent");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "C", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	rt_close(&d);
	RT_CHECK(err, "unlink C");

	if (diff_finish(3, 2, 3, 3, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D13: dissolving a pair to zero links is two records, both
 * DELETE x REMOVED.
 */
static int
test_diff_dissolve(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D13: dissolve pair, two records");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "A");
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "B");
	rt_close(&d);
	RT_CHECK(err, "dissolve");

	if (diff_finish(2, 0, 2, 2, 2, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * Shared fixture for D14/D16: two hardlink pairs P1 {A, B} and
 * P2 {C, D}, then the left relinks B onto P2's dnode. The two
 * pools' contents are the caller's lever: different content makes
 * the moved path an EDIT x MOVED, identical content a linkpool-
 * only NONE x MOVED.
 */
static int
diff_relink_fixture(const char *p1_data, const char *p2_data,
    uint64_t len)
{
	rt_ds_t d;
	uint64_t o1, o2;
	int err;

	err = rt_scaffold_empty_base();
	if (err != 0)
		return (err);

	err = rt_open(RT_DS_SRC, &d);
	if (err != 0)
		return (err);
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", p1_data, len,
	    &o1);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", o1);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "C",
		    p2_data, len, &o2);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "D", o2);
	rt_close(&d);
	if (err != 0)
		return (err);

	err = rt_scaffold_snap_and_clone();
	if (err != 0)
		return (err);

	err = rt_open(RT_DS_LEFT, &d);
	if (err != 0)
		return (err);
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "C", &o2);
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "B");
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", o2);
	rt_close(&d);
	return (err);
}

/*
 * D14: relink into another linkpool with DIFFERENT content: the
 * moved path is EDIT x MOVED, the abandoned mate a linkpool-only
 * NONE x REMOVED, and the target pool's paths produce nothing.
 */
static int
test_diff_relink_differs(void)
{
	TEST_START("D14: relink to other pool, content differs");
	RT_CHECK(diff_relink_fixture("aaaa", "cccc", 4),
	    "relink fixture");
	if (diff_finish(4, 3, 4, 4, 2, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D15: a recycled shared dnode (gen flipped on a linkpool's
 * object) breaks lineage for every member path: each classifies
 * EDIT x MOVED with numerically equal from/to.
 */
static int
test_diff_recycled_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D15: recycled pool dnode, two records");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "A", &obj);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, obj, ZPL_GEN, 777777);
	rt_close(&d);
	RT_CHECK(err, "flip gen");

	if (diff_finish(2, 0, 2, 2, 2, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D16: relink into another linkpool with IDENTICAL content: the
 * moved path is a linkpool-only NONE x MOVED record -- membership
 * changed while content hysteria says nothing happened.
 */
static int
test_diff_relink_identical(void)
{
	TEST_START("D16: relink to other pool, content identical");
	RT_CHECK(diff_relink_fixture("same\n", "same\n", 5),
	    "relink fixture");
	if (diff_finish(4, 4, 4, 4, 2, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D17: side symmetry -- D2's edit built on the RIGHT. Only the
 * right changelist may move.
 */
static int
test_diff_right_side(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D17: right-side edit, right record only");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "WORLD\n", 6);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	if (diff_finish(3, 3, 2, 0, 0, 1))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D18: both sides act on disjoint paths; each side's changelist
 * counts only its own work.
 */
static int
test_diff_both_sides(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("D18: both sides act, one record each");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "WORLD\n", 6);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "rfile", "r\n", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	if (diff_finish(4, 2, 3, 0, 1, 1))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * D19: a side-added subtree containing a hardlink pair: the dir's
 * own ADD x NONE plus two ADD x ADDED paths -- three records, all
 * with no base slot anywhere.
 */
static int
test_diff_added_subtree(void)
{
	rt_ds_t d;
	uint64_t dir, obj;
	int err;

	TEST_START("D19: added subtree with hardlink pair");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "ldir", &dir);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dir, "h1", "h\n", 2,
		    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, dir, "h2", obj);
	rt_close(&d);
	RT_CHECK(err, "build subtree");

	if (diff_finish(6, 3, 3, 2, 3, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

void
run_diff_tests(void)
{
	(void) printf("\n[diff: D matrix, two-axis record "
	    "assertions]\n");
	(void) test_diff_untouched();
	(void) test_diff_edit();
	(void) test_diff_hysterical_zero();
	(void) test_diff_add();
	(void) test_diff_delete();
	(void) test_diff_rename_collapses();
	(void) test_diff_recreate_one_record();
	(void) test_diff_dir_chmod();
	(void) test_diff_dir_entries();
	(void) test_diff_sever_identical();
	(void) test_diff_join();
	(void) test_diff_unlink_survivors();
	(void) test_diff_dissolve();
	(void) test_diff_relink_differs();
	(void) test_diff_recycled_pool();
	(void) test_diff_relink_identical();
	(void) test_diff_right_side();
	(void) test_diff_both_sides();
	(void) test_diff_added_subtree();
}
