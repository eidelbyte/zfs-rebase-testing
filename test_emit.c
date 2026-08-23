// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Content-merge-emit matrix (E content, F warnings/actions/emit)
 * tests -- see TEST-MATRIX.md, "Content-merge-emit matrix". Each
 * test's comment names the cell(s) it covers. The samepath (P)
 * family is covered by the revived crossref-era sections.
 *
 * Phases E and F are observed through the manifest itself:
 * rt_run_rebase() returns the outnvl and the accessors read
 * counts, typed conflicts, and typed warnings. The action list
 * never crosses to userland; nactions pins its size.
 */

#include "rebase_test.h"

/*
 * E1 (+E2 at scale): THE PHANTOM-CONFLICT DISSOLUTION, the second
 * acceptance test of the rewrite. Base linkpool {A,B,C,X,Y} at v1;
 * left edits through its {A,B,C} view to v2 and severs X,Y at v1;
 * right edits through its {C,X,Y} view to the same v2 and severs
 * A,B at v1. Path-level diffing would see four false conflicts;
 * linkpool-level sees one lineage edited convergently plus
 * disjoint severs: ZERO conflicts. The edit traveled through the
 * hardlink, not the path. Side facts pinned too: right's severs
 * compile SEVER actions, and left's winning edit on a roster the
 * severs shrank draws one LINKPOOL_SHRUNK warning.
 */
static int
test_emit_phantom_dissolution(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("E1: phantom-conflict dissolution (acceptance)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "A", "v1", 2,
	    &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "C", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "X", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "Y", obj));
	rt_close(&d);
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_edit_file(d.rtd_os, obj, "v2", 2));
	VERIFY0(rt_hysterical_edit(d.rtd_os, d.rtd_root, "X", "v1",
	    2, NULL));
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "Y", "v1", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "left edit + severs");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_edit_file(d.rtd_os, obj, "v2", 2));
	VERIFY0(rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "v1",
	    2, NULL));
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "B", "v1", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "right edit + severs");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected ZERO conflicts (phantom dissolution)");
	TEST_EXPECT(rt_manifest_has_warning(nvl, "LINKPOOL_SHRUNK",
	    "A") || rt_manifest_has_warning(nvl, "LINKPOOL_SHRUNK",
	    "B"),
	    "expected LINKPOOL_SHRUNK for a right-severed member");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 2,
	    "expected 2 SEVER actions (right's severs)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * E2: both sides edit a pool to the same bytes -- convergent,
 * clean, and nothing to do (no actions, no warnings).
 */
static int
test_emit_convergent_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("E2: convergent pool edits, clean");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "g", obj));
	rt_close(&d);
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_edit_file(d.rtd_os, obj, "same!", 5);
	rt_close(&d);
	RT_CHECK(err, "left edit");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "same!", 5);
	rt_close(&d);
	RT_CHECK(err, "right edit");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (convergent)");
	TEST_EXPECT(rt_manifest_nwarnings(nvl) == 0,
	    "expected 0 warnings");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 0,
	    "expected 0 actions");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * E3 + F1: a right-only pool edit wins cleanly, compiles one
 * WRITE action, and draws one IMPLIED_CHANGE warning per member
 * path the left branch never looked at.
 */
static int
test_emit_right_pool_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("E3+F1: right pool edit = WRITE + IMPLIED");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1,
	    &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "g", obj));
	rt_close(&d);
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "right edit");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 1,
	    "expected 1 WRITE action");
	TEST_EXPECT(rt_manifest_nwarnings(nvl) == 2,
	    "expected IMPLIED_CHANGE per member");
	TEST_EXPECT(rt_manifest_has_warning(nvl, "IMPLIED_CHANGE",
	    "f"), "expected IMPLIED_CHANGE at f");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * E6: a one-sided split merges fully three-way. Left severs {c,d}
 * out of a four-member pool (a fragment frozen at base content);
 * right edits the parent. The fragment's content equals base, so
 * right's edit wins for BOTH groups -- the split changes
 * membership, never content ownership. Zero conflicts, a WRITE
 * per group, IMPLIED_CHANGE for the parent's left-silent members,
 * and one SHRUNK for the roster the split took from the editor.
 */
static int
test_emit_fragment_three_way(void)
{
	rt_ds_t d;
	uint64_t obj, z;
	nvlist_t *nvl;
	int err;

	TEST_START("E6: one-sided split merges three-way");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "a", "x", 1,
	    &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "b", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "c", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "d", obj));
	rt_close(&d);
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "c"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "d"));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "c", "x", 1,
	    &z));
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "d", z);
	rt_close(&d);
	RT_CHECK(err, "left split");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "newdata", 7);
	rt_close(&d);
	RT_CHECK(err, "right edit");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (three-way through the parent)");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 2,
	    "expected a WRITE per group");
	TEST_EXPECT(rt_manifest_nwarnings(nvl) == 3,
	    "expected IMPLIED a,b + one SHRUNK");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * F2: LINKPOOL_SHRUNK -- left severs A, right edits the pool
 * content; the sever wins its path, the edit wins the content,
 * and the editor is told its linkpool shrank (the doc's
 * sever-vs-edit orthogonality variant, warning included).
 */
static int
test_emit_shrunk_warning(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("F2: winner's roster shrank = SHRUNK warning");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "A", "x", 1,
	    &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, d.rtd_root, "C", obj));
	rt_close(&d);
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "A", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "left sever");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, obj, "edited!", 7);
	rt_close(&d);
	RT_CHECK(err, "right edit");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (orthogonal intents)");
	TEST_EXPECT(rt_manifest_has_warning(nvl, "LINKPOOL_SHRUNK",
	    "A"), "expected LINKPOOL_SHRUNK at A");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 1,
	    "expected 1 WRITE action");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * F3: unconflicted standalone right-side changes compile into the
 * action list -- an add, an edit, and a delete become COPY, WRITE,
 * and UNLINK.
 */
static int
test_emit_standalone_actions(void)
{
	rt_ds_t d;
	uint64_t obj, subdir;
	nvlist_t *nvl;
	int err;

	TEST_START("F3: right standalone changes = 3 actions");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "r1", "n\n", 2,
	    NULL));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_edit_file(d.rtd_os, obj, "redit!\n", 7));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir",
	    &subdir));
	err = rt_remove_entry(d.rtd_os, subdir, "inner");
	rt_close(&d);
	RT_CHECK(err, "right changes");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 3,
	    "expected COPY + WRITE + UNLINK");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * F4: left-expressed changes compile NO actions -- the left HEAD
 * is the apply target and already carries its own work.
 */
static int
test_emit_left_no_actions(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("F4: left changes compile no actions");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "l1", "n\n", 2,
	    NULL));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "ledit!\n", 7);
	rt_close(&d);
	RT_CHECK(err, "left changes");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 0,
	    "expected 0 actions");
	TEST_EXPECT(rt_manifest_nwarnings(nvl) == 0,
	    "expected 0 warnings");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * F5 + F8: the classic silent casualty -- left renames a file,
 * right adds a symlink to the old name. The link survives, its
 * target does not: DANGLING_SYMLINK. The control link pointing at
 * the NEW name resolves through the move and stays quiet.
 */
static int
test_emit_dangling_symlink(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("F5+F8: merge-caused dangling symlink");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2");
	rt_close(&d);
	RT_CHECK(err, "left rename");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_create_symlink(d.rtd_os, d.rtd_root, "ln",
	    "hello", NULL));
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "ln2",
	    "/hello2", NULL);
	rt_close(&d);
	RT_CHECK(err, "right symlinks");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts");
	TEST_EXPECT(rt_manifest_has_warning(nvl,
	    "DANGLING_SYMLINK", "ln"),
	    "expected DANGLING_SYMLINK at ln");
	TEST_EXPECT(rt_manifest_nwarnings(nvl) == 1,
	    "expected exactly one warning (ln2 resolves)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * F6: a symlink that was already dangling in base stays quiet --
 * the dangle is pre-existing, not merge-caused.
 */
static int
test_emit_dangling_preexisting(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("F6: pre-existing dangle is suppressed");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "ln",
	    "ghost", NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nwarnings(nvl) == 0,
	    "expected 0 warnings (pre-existing dangle)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * F7: relative targets resolve against the symlink's parent
 * directory -- a subdir link to "../hello" dangles when the merge
 * (here, left's own delete) removes /hello.
 */
static int
test_emit_dangling_relative(void)
{
	rt_ds_t d;
	uint64_t subdir;
	nvlist_t *nvl;
	int err;

	TEST_START("F7: relative target resolves via parent");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "w\n", 2, NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "subdir",
	    &subdir));
	err = rt_create_symlink(d.rtd_os, subdir, "ln", "../hello",
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "left delete");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_has_warning(nvl,
	    "DANGLING_SYMLINK", "subdir/ln"),
	    "expected DANGLING_SYMLINK at subdir/ln");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_emit_tests(void)
{
	(void) printf("\n[emit: crossref phases E+F (E/F matrix)]\n");
	(void) test_emit_phantom_dissolution();
	(void) test_emit_convergent_pool();
	(void) test_emit_right_pool_edit();
	(void) test_emit_fragment_three_way();
	(void) test_emit_shrunk_warning();
	(void) test_emit_standalone_actions();
	(void) test_emit_left_no_actions();
	(void) test_emit_dangling_symlink();
	(void) test_emit_dangling_preexisting();
	(void) test_emit_dangling_relative();
}
