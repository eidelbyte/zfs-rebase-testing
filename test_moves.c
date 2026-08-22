// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Move detection (sprint-2: the move-collapse phase): renames that
 * must collapse to MOVE / MOVE_EDIT, the conflicts moves can cause
 * (divergent destinations, move against edit or delete), and the
 * benign both-sides-agree cases.
 */

#include "rebase_test.h"

/*
 * Pure rename: remove "hello", add "hello_renamed" pointing at the
 * same dnode. Must collapse to a MOVE.
 */
static int
test_left_move(void)
{
	rt_ds_t d;
	int err;

	TEST_START("left moves (renames) a file");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello_renamed");
	rt_close(&d);
	RT_CHECK(err, "rename hello");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Rename plus content change: must collapse to MOVE_EDIT.
 */
static int
test_move_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("move + edit on left");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello_moved"));
	err = rt_edit_file(d.rtd_os, obj, "moved and edited\n", 17);
	rt_close(&d);
	RT_CHECK(err, "edit moved file");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Same file moved to different destinations on each side.
 */
static int
test_conflict_move_diverge_diff_dest(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: MOVE_DIVERGE (diff dest)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello_left");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello_right");
	rt_close(&d);
	RT_CHECK(err, "rename right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_DIVERGE",
	    "hello_left") ||
	    rt_manifest_has_conflict(nvl, "MOVE_DIVERGE",
	    "hello_right"),
	    "expected MOVE_DIVERGE");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Different files moved to the same destination.
 */
static int
test_conflict_move_diverge_same_dest(void)
{
	rt_ds_t d;
	uint64_t subdir;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: MOVE_DIVERGE (same dest)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	/* Left renames "hello" to "target". */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "target");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	/* Right moves "subdir/inner" to "target" in root. */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	err = rt_rename_file(d.rtd_os, subdir, "inner",
	    d.rtd_root, "target");
	rt_close(&d);
	RT_CHECK(err, "rename right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_DIVERGE",
	    "target"),
	    "expected MOVE_DIVERGE at target");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left moves onto a path where right created a new file.
 */
static int
test_conflict_move_vs_edit_dest(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: MOVE_VS_EDIT (dest collision)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "target");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "target",
	    "blocking\n", 9, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "target"),
	    "expected MOVE_VS_EDIT at target");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left moves "hello", right edits the same dnode in place.
 */
static int
test_conflict_move_vs_edit_obj(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: MOVE_VS_EDIT (obj-based)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello_moved");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "right-edit\n", 11);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "hello_moved"),
	    "expected MOVE_VS_EDIT");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides move the same file to the same destination: benign.
 */
static int
test_benign_both_move_same_dest(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("benign: both MOVE same dest");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "renamed");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "renamed");
	rt_close(&d);
	RT_CHECK(err, "rename right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical move)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left does MOVE, right does MOVE_EDIT to the same dest, same obj:
 * benign. The right-only edit is not a conflict (the apply phase
 * handles it).
 */
static int
test_benign_move_plus_move_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("benign: MOVE + MOVE_EDIT same dest");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	/* Left: pure rename. */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "renamed");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	/* Right: rename + edit. */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "renamed"));
	err = rt_edit_file(d.rtd_os, obj, "edited too\n", 11);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (MOVE + MOVE_EDIT benign)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left moves "hello" to "target"; right creates a NEW file at
 * "target" (a name not in base). Left carries MOVE at "target"
 * (from "hello"), right carries ADD at "target": the path merge
 * sees MOVE vs ADD at the same destination.
 */
static int
test_edge_move_and_create_at_dest(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: left MOVE, right ADD at same dest");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "target");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "target",
	    "right-new\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "target"),
	    "expected MOVE_VS_EDIT at target");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left moves the dnode, right deletes its old path.
 */
static int
test_edge_move_vs_delete(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: left MOVE, right DELETE same dnode");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "moved");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "delete right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "moved"),
	    "expected MOVE_VS_EDIT");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides move+edit the same file to the same destination, but
 * the edits differ: not suppressed, both-modified at the new path.
 */
static int
test_edge_both_move_edit_different(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both MOVE_EDIT same dest, diff edits");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_edit_file(d.rtd_os, obj, "left-edit\n", 10);
	rt_close(&d);
	RT_CHECK(err, "move+edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_edit_file(d.rtd_os, obj, "right-edit\n", 11);
	rt_close(&d);
	RT_CHECK(err, "move+edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello2"),
	    "expected BOTH_MODIFIED at hello2");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * The mirror image of move-vs-delete: right moves, left deletes.
 * Exercises the right-to-left direction of the move cross-check.
 */
static int
test_edge_right_move_left_delete(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: right MOVE, left DELETE same dnode");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "delete left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "moved");
	rt_close(&d);
	RT_CHECK(err, "rename right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "moved"),
	    "expected MOVE_VS_EDIT at moved");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_moves_tests(void)
{
	(void) printf("\n[moves: rename detection, move conflicts]\n");
	(void) test_left_move();
	(void) test_move_edit();
	(void) test_conflict_move_diverge_diff_dest();
	(void) test_conflict_move_diverge_same_dest();
	(void) test_conflict_move_vs_edit_dest();
	(void) test_conflict_move_vs_edit_obj();
	(void) test_benign_both_move_same_dest();
	(void) test_benign_move_plus_move_edit();
	(void) test_edge_move_and_create_at_dest();
	(void) test_edge_move_vs_delete();
	(void) test_edge_both_move_edit_different();
	(void) test_edge_right_move_left_delete();
}
