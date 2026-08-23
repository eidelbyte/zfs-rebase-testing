// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Move-collapse matrix (M) tests -- see TEST-MATRIX.md,
 * "Move-collapse matrix". Each test's comment names the cell it
 * covers. Collapses are observed through the walk's third dbgmsg
 * line via rt_move_stats(); every M test asserts the full
 * ten-tuple (the D six-tuple plus moves/move-edits per side), so
 * a collapse is visible twice: a move counted and a record gone
 * from the post-collapse changelist counts.
 *
 * The manifest-based tests in the second half of this file are
 * crossref-era: they assert conflict output that does not exist
 * until the cross-reference and emit issues land, and they fail
 * (documented) until then.
 */

#include "rebase_test.h"

/*
 * The M-cell finisher is the shared ten-tuple comparator from
 * test_diff.c, under this section's own name.
 */
#define	moves_finish	diff_finish_full

/*
 * M1: pure standalone rename collapses to one MOVE. The rename
 * helper leaves the dnode clean, so the content gate settles on
 * the untouched-since-fork fast path. Also D20's witness: the two
 * records can only have met by object number.
 */
static int
test_moves_pure_rename(void)
{
	rt_ds_t d;
	int err;

	TEST_START("M1: pure rename = one MOVE");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2");
	rt_close(&d);
	RT_CHECK(err, "rename");

	if (moves_finish(4, 2, 3, 0, 1, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M2: rename plus content edit collapses to one MOVE_EDIT.
 */
static int
test_moves_move_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M2: rename + edit = one MOVE_EDIT");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello_moved"));
	err = rt_edit_file(d.rtd_os, obj, "moved and edited\n", 17);
	rt_close(&d);
	RT_CHECK(err, "edit moved file");

	if (moves_finish(4, 2, 3, 0, 1, 0, 0, 0, 1, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M3: rename plus a timestamp touch is a MOVE, not a MOVE_EDIT.
 * The touch dirties the dnode, so the content gate cannot take the
 * fast path; it must run the full tiers, and timestamps are
 * excluded from SA identity.
 */
static int
test_moves_rename_touched(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M3: rename + touch = MOVE (full tiers)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_touch(d.rtd_os, obj);
	rt_close(&d);
	RT_CHECK(err, "touch");

	if (moves_finish(4, 2, 3, 0, 1, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M4: cross-directory move collapses like a same-directory rename
 * (path-scoped records, object-scoped matching).
 */
static int
test_moves_cross_dir(void)
{
	rt_ds_t d;
	uint64_t subdir;
	int err;

	TEST_START("M4: cross-directory move = one MOVE");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir",
	    &subdir));
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    subdir, "hello_in");
	rt_close(&d);
	RT_CHECK(err, "move into subdir");

	if (moves_finish(4, 2, 3, 0, 1, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M5: a renamed directory and its child each collapse: two MOVE
 * records. Per-descendant records are the contract; deduplicating
 * them under the directory's single ZAP rename belongs to the
 * apply compiler, not here.
 */
static int
test_moves_dir_rename(void)
{
	rt_ds_t d;
	int err;

	TEST_START("M5: dir rename = MOVEs for dir and child");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "subdir",
	    d.rtd_root, "subdir2");
	rt_close(&d);
	RT_CHECK(err, "rename dir");

	if (moves_finish(5, 1, 3, 0, 2, 0, 2, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M6: member-path rename -- the (REMOVED(from=N), ADDED(to=N))
 * guard shape. One link out, one link in, same dnode: refcount
 * neutral, collapses to a MOVE; the mate path yields no record.
 */
static int
test_moves_member_rename(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M6: member-path rename collapses");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "g", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "f",
	    d.rtd_root, "f2");
	rt_close(&d);
	RT_CHECK(err, "rename member");

	if (moves_finish(3, 1, 2, 3, 1, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M7: guard mixed shape (NONE, ADDED) -- a standalone file renamed
 * while a new hardlink lands on its dnode. The DELETE left the
 * base file standalone but the ADDs joined a linkpool: a genuine
 * membership change rides beside the rename, so nothing collapses
 * and all three records reach cross-reference.
 */
static int
test_moves_guard_added(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M7: guard blocks (NONE, ADDED) pair");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "f",
	    d.rtd_root, "g"));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "g2", obj);
	rt_close(&d);
	RT_CHECK(err, "link g2");

	if (moves_finish(3, 0, 1, 2, 3, 0, 0, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M8: guard mixed shape (REMOVED, NONE) -- a member path renamed
 * while its pool dissolves (the mate unlinked). The ADD is
 * standalone but the DELETEs left a linkpool: no collapse, the
 * rename is deliberately not detected (conservative, documented).
 */
static int
test_moves_guard_dissolved(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M8: guard blocks (REMOVED, NONE) pair");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "h", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "f",
	    d.rtd_root, "g"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "h");
	rt_close(&d);
	RT_CHECK(err, "unlink mate");

	if (moves_finish(3, 0, 2, 2, 3, 0, 0, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M9: gen mismatch means recycled lineage: the pair must NOT
 * collapse -- and especially must not become a MOVE_EDIT, which is
 * what trusting the content tiers alone would produce. Both
 * records survive.
 */
static int
test_moves_gen_mismatch(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M9: gen mismatch never collapses");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_set_sa_u64(d.rtd_os, obj, ZPL_GEN, 424242);
	rt_close(&d);
	RT_CHECK(err, "flip gen");

	if (moves_finish(4, 2, 3, 0, 2, 0, 0, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M10: unreadable gen at collapse time is EIO. A rename's two
 * records never read ZPL_GEN during classification (ADD and DELETE
 * skip the hysteria tiers), so the collapse gen gate is the FIRST
 * reader -- this pins the error path that only move-collapse
 * exercises.
 */
static int
test_moves_gen_missing(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M10: missing gen at collapse = EIO");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
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
 * M11: several eligible DELETE candidates -- a pool of three with
 * one member renamed and another unlinked. Exactly one pair forms
 * (the collapse count proves it) and the other DELETE survives as
 * a genuine unlink. WHICH delete was consumed is invisible in
 * counts: that is M18, deferred to emit. Side effect worth the
 * cell on its own: the unlink dirties the shared dnode's nlink,
 * and the content gate must still say MOVE, never MOVE_EDIT --
 * ZPL_LINKS is excluded from SA identity.
 */
static int
test_moves_two_delete_candidates(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M11: one MOVE + one surviving DELETE");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "h", obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "k", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "f",
	    d.rtd_root, "f2"));
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "k");
	rt_close(&d);
	RT_CHECK(err, "unlink k");

	if (moves_finish(4, 1, 3, 4, 2, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M12: several ADDs drain several DELETEs -- both members of a
 * pair renamed, two independent collapses in one object run.
 */
static int
test_moves_both_members_renamed(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M12: both members renamed = two MOVEs");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "p", "x", 1,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "q", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "p",
	    d.rtd_root, "p2"));
	err = rt_rename_file(d.rtd_os, d.rtd_root, "q",
	    d.rtd_root, "q2");
	rt_close(&d);
	RT_CHECK(err, "rename q");

	if (moves_finish(4, 0, 2, 4, 2, 0, 2, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M13: side independence -- both sides rename the same file to
 * different names; each changelist collapses its own pair. The
 * counter-level half of the future MOVE_DIVERGE fixture.
 */
static int
test_moves_both_sides(void)
{
	rt_ds_t d;
	int err;

	TEST_START("M13: both sides collapse independently");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "h_left");
	rt_close(&d);
	RT_CHECK(err, "rename left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "h_right");
	rt_close(&d);
	RT_CHECK(err, "rename right");

	if (moves_finish(5, 2, 2, 0, 1, 1, 1, 1, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M14: swapping two files exchanges objects at two paths and
 * produces two EDIT records -- there is no ADD or DELETE to pair,
 * so nothing collapses. v1 limit, documented: content ops are
 * path-scoped, and swap lineage is not tracked. (The transient
 * "tmp" name is gone before the fence snapshot; the dir shuffle
 * dirties subdir's dnode, which must still classify hysterical.)
 */
static int
test_moves_swap(void)
{
	rt_ds_t d;
	uint64_t subdir;
	int err;

	TEST_START("M14: swap = two EDITs, zero moves");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir",
	    &subdir));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "tmpname"));
	VERIFY0(rt_rename_file(d.rtd_os, subdir, "inner",
	    d.rtd_root, "hello"));
	err = rt_rename_file(d.rtd_os, d.rtd_root, "tmpname",
	    subdir, "inner");
	rt_close(&d);
	RT_CHECK(err, "swap");

	if (moves_finish(3, 1, 3, 0, 2, 0, 0, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M15: rename with a new file created at the old path. The old
 * path classifies EDIT (different dnode now lives there), so the
 * renamed object's run has an ADD but no DELETE: no collapse, the
 * ADD survives. v1 limit, documented.
 */
static int
test_moves_replaced_source(void)
{
	rt_ds_t d;
	int err;

	TEST_START("M15: replaced source = EDIT + ADD, no MOVE");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "fresh contents\n", 15, NULL);
	rt_close(&d);
	RT_CHECK(err, "create replacement");

	if (moves_finish(4, 2, 3, 0, 2, 0, 0, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * M17: symlink rename collapses to a MOVE. The touch forces the
 * content gate off the fast path and through the full tiers, so
 * the var-attr identity compare (ZPL_SYMLINK) runs inside the
 * collapse.
 */
static int
test_moves_symlink_rename(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("M17: symlink rename = MOVE");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "ln",
	    "target/path", NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "ln", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "ln",
	    d.rtd_root, "ln2"));
	err = rt_touch(d.rtd_os, obj);
	rt_close(&d);
	RT_CHECK(err, "touch symlink");

	if (moves_finish(2, 0, 1, 0, 1, 0, 1, 0, 0, 0))
		TEST_FAIL("tuple mismatch");
	TEST_PASS();
}

/*
 * ----------------------------------------------------------------
 * Crossref-era tests from here down: manifest-based assertions
 * that stay red (documented) until cross-reference and emit land.
 * They will be re-plotted into the crossref matrix then.
 * ----------------------------------------------------------------
 */

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
	(void) printf("\n[moves: collapse (M matrix), crossref-era conflicts]\n");
	(void) test_moves_pure_rename();
	(void) test_moves_move_edit();
	(void) test_moves_rename_touched();
	(void) test_moves_cross_dir();
	(void) test_moves_dir_rename();
	(void) test_moves_member_rename();
	(void) test_moves_guard_added();
	(void) test_moves_guard_dissolved();
	(void) test_moves_gen_mismatch();
	(void) test_moves_gen_missing();
	(void) test_moves_two_delete_candidates();
	(void) test_moves_both_members_renamed();
	(void) test_moves_both_sides();
	(void) test_moves_swap();
	(void) test_moves_replaced_source();
	(void) test_moves_symlink_rename();
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
