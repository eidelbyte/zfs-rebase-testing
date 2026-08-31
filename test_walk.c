// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Walk-phase matrix: the three-way union iteration, directory
 * recursion, tree shapes, orphan skipping, and error propagation.
 * Cells W1-W18 in TEST-MATRIX.md; each test names the cells it
 * covers. These tests assert only the engine's return code (success
 * = the walk completed; specific errnos for the fault cells), so
 * they are meaningful from zap-walk-basic onward. The linkpool
 * discovery and completeness cells (the LP and LV rows) live in
 * test_linkpool.c.
 */

#include "rebase_test.h"

/*
 * W1-W7: the full per-name presence matrix. Every combination of
 * (left, base, right) presence for one name, all in one tree:
 *
 *   name     left base right  built by
 *   p_all     Y    Y    Y     in src, untouched
 *   p_lb      Y    Y    -     in src; right removes
 *   p_br      -    Y    Y     in src; left removes
 *   p_lr      Y    -    Y     both sides create (same name)
 *   p_l       Y    -    -     left creates
 *   p_r       -    -    Y     right creates
 *   p_b       -    Y    -     in src; both remove
 *
 * Exercises all three phases of rebase_walk_dir(): p_all/p_lb/
 * p_br/p_b through the base cursor, p_lr/p_l through the left
 * cursor, p_r through the right cursor.
 */
static int
test_walk_presence_matrix(void)
{
	rt_ds_t d;
	int err;

	TEST_START("walk: presence matrix (7 combos)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_all",
	    "a\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_lb",
	    "b\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_br",
	    "c\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_b",
	    "d\n", 2, NULL));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	RT_CHECK(rt_open(RT_DS_OFFOF, &d), "hold left");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "p_br"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "p_b"));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_lr",
	    "from-left\n", 10, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_l",
	    "left-only\n", 10, NULL));
	rt_close(&d);

	RT_CHECK(rt_open(RT_DS_ONTO, &d), "hold right");
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "p_lb"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "p_b"));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_lr",
	    "from-right\n", 11, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "p_r",
	    "right-only\n", 11, NULL));
	rt_close(&d);

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

/*
 * Replace one root entry with the other kind (file <-> dir),
 * preserving the name. Used to build the kind-mix cells.
 */
static int
replace_kind(objset_t *os, uint64_t root, const char *name,
    boolean_t make_dir)
{
	int err;

	err = rt_remove_entry(os, root, name);
	if (err != 0)
		return (err);
	if (make_dir)
		return (rt_create_dir(os, root, name, NULL));
	return (rt_create_file(os, root, name, "swap\n", 5, NULL));
}

/*
 * W8-W10: the same-name kind matrix. For a name present on all
 * three sides, every mixed assignment of {file, dir} (the two
 * uniform cases are W1's p_all and the recursed dir below):
 *
 *   name    left base right      name    left base right
 *   k_dff    dir file file       k_ffd   file file dir
 *   k_fdf   file  dir file       k_ddf    dir  dir file
 *   k_dfd    dir file  dir       k_fdd   file  dir  dir
 *
 * Plus: k_add_mix (left creates dir X, right creates file X, no
 * base), d_rec (dir on all three, children changed on each side --
 * unchanged-parent recursion), and d_empty (empty dir everywhere).
 * Mixed slots must recurse only the dir slots and run linkpool
 * accounting only on the file slots.
 */
static int
test_walk_kind_matrix(void)
{
	rt_ds_t d;
	uint64_t rec;
	int err;

	TEST_START("walk: same-name kind matrix");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	/* base kinds: middle letter of each cell name */
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "k_dff",
	    "x\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "k_ffd",
	    "x\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "k_dfd",
	    "x\n", 2, NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "k_fdf", NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "k_ddf", NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "k_fdd", NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "d_rec", &rec));
	VERIFY0(rt_create_file(d.rtd_os, rec, "seed",
	    "s\n", 2, NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "d_empty", NULL));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	RT_CHECK(rt_open(RT_DS_OFFOF, &d), "hold left");
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_dff", B_TRUE));
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_dfd", B_TRUE));
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_fdf", B_FALSE));
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_fdd", B_FALSE));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "k_add_mix",
	    NULL));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "d_rec", &rec));
	VERIFY0(rt_create_file(d.rtd_os, rec, "lchild",
	    "l\n", 2, NULL));
	rt_close(&d);

	RT_CHECK(rt_open(RT_DS_ONTO, &d), "hold right");
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_ffd", B_TRUE));
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_dfd", B_TRUE));
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_ddf", B_FALSE));
	VERIFY0(replace_kind(d.rtd_os, d.rtd_root, "k_fdf", B_FALSE));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "k_add_mix",
	    "f\n", 2, NULL));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "d_rec", &rec));
	VERIFY0(rt_create_file(d.rtd_os, rec, "rchild",
	    "r\n", 2, NULL));
	rt_close(&d);

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	/*
	 * k_ffd is a right-only dir-ization with a silent left --
	 * the one cell here whose replay the apply phase REFUSES
	 * in v1 (AE10's dir-flip refusal). The walk itself
	 * classified every cell before that refusal fired, which
	 * is this test's subject; any other error still fails.
	 */
	TEST_EXPECT(err == EOPNOTSUPP,
	    "expected the AE10 dir-flip refusal");
	TEST_PASS();
}

/*
 * W11-W13: side-only subtrees. Left adds a nested tree (including
 * a hardlink pair inside it: linkpool discovery through phase-2
 * recursion, cell LP2b), right adds a different nested tree, and
 * both add same-name directories with different children (dir
 * recursion with no base slot at all).
 */
static int
test_walk_side_subtrees(void)
{
	rt_ds_t d;
	uint64_t dir, sub, obj;
	int err;

	TEST_START("walk: side-only subtrees");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_OFFOF, &d), "hold left");
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "ldir", &dir));
	VERIFY0(rt_create_file(d.rtd_os, dir, "a", "a\n", 2, NULL));
	VERIFY0(rt_create_dir(d.rtd_os, dir, "ldir2", &sub));
	VERIFY0(rt_create_file(d.rtd_os, sub, "c", "c\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, dir, "h1", "h\n", 2, &obj));
	VERIFY0(rt_add_hardlink(d.rtd_os, sub, "h2", obj));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "bdir", &dir));
	VERIFY0(rt_create_file(d.rtd_os, dir, "lf", "l\n", 2, NULL));
	rt_close(&d);

	RT_CHECK(rt_open(RT_DS_ONTO, &d), "hold right");
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "rdir", &dir));
	VERIFY0(rt_create_dir(d.rtd_os, dir, "rsub", &sub));
	VERIFY0(rt_create_file(d.rtd_os, sub, "y", "y\n", 2, NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "bdir", &dir));
	VERIFY0(rt_create_file(d.rtd_os, dir, "rf", "r\n", 2, NULL));
	rt_close(&d);

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

/*
 * W14: deleted directory trees. One base dir removed (with its
 * contents) on the left only, another removed on both sides.
 */
static int
test_walk_deleted_dir_trees(void)
{
	rt_ds_t d;
	uint64_t dir;
	int err;

	TEST_START("walk: deleted directory trees");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "ddel", &dir));
	VERIFY0(rt_create_file(d.rtd_os, dir, "one", "1\n", 2, NULL));
	VERIFY0(rt_create_file(d.rtd_os, dir, "two", "2\n", 2, NULL));
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "dboth", &dir));
	VERIFY0(rt_create_file(d.rtd_os, dir, "z", "z\n", 2, NULL));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	RT_CHECK(rt_open(RT_DS_OFFOF, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "ddel", &dir));
	VERIFY0(rt_remove_entry(d.rtd_os, dir, "one"));
	VERIFY0(rt_remove_entry(d.rtd_os, dir, "two"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "ddel"));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "dboth", &dir));
	VERIFY0(rt_remove_entry(d.rtd_os, dir, "z"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "dboth"));
	rt_close(&d);

	RT_CHECK(rt_open(RT_DS_ONTO, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "dboth", &dir));
	VERIFY0(rt_remove_entry(d.rtd_os, dir, "z"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "dboth"));
	rt_close(&d);

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

/*
 * W15: deep nesting -- six directory levels, edits at the leaf on
 * both sides. Path building and recursion depth.
 */
static int
test_walk_deep_nesting(void)
{
	rt_ds_t d;
	uint64_t dir, obj;
	int err;

	TEST_START("walk: deep nesting");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	dir = d.rtd_root;
	{
		const char *levels[] = { "a", "b", "c", "d", "e", "f" };
		for (int i = 0; i < 6; i++)
			VERIFY0(rt_create_dir(d.rtd_os, dir,
			    levels[i], &dir));
	}
	VERIFY0(rt_create_file(d.rtd_os, dir, "leaf",
	    "deep\n", 5, NULL));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	RT_CHECK(rt_open(RT_DS_OFFOF, &d), "hold left");
	dir = d.rtd_root;
	{
		const char *levels[] = { "a", "b", "c", "d", "e", "f" };
		for (int i = 0; i < 6; i++)
			VERIFY0(rt_dir_lookup(d.rtd_os, dir,
			    levels[i], &dir));
	}
	VERIFY0(rt_dir_lookup(d.rtd_os, dir, "leaf", &obj));
	VERIFY0(rt_edit_file(d.rtd_os, obj, "left-deep\n", 10));
	rt_close(&d);

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

/*
 * W16: completely empty filesystems -- root ZAPs with no entries
 * on any side.
 */
static int
test_walk_empty_filesystems(void)
{
	int err;

	TEST_START("walk: empty filesystems");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

/*
 * W17: delete-queue orphan simulation (planning-doc catalog test
 * 13's walk half). A pathless object with nlink == 0 exists in all
 * three snapshots; the path-driven walk must never encounter it
 * and the linkpool verify must be unaffected.
 */
static int
test_walk_orphan_skipped(void)
{
	rt_ds_t d;
	int err;

	TEST_START("walk: pathless orphan never visited");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "doomed",
	    "gone\n", 5, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "keeper",
	    "kept\n", 5, NULL));
	/* nlink drops to 0; the dnode stays allocated, pathless */
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "doomed"));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

/*
 * W18: error propagation -- a directory entry pointing at an
 * object that was never allocated. A dangling reference is
 * corruption, so the rebase aborts with EIO (never a leaked
 * ENOENT, which callers read as "no common ancestor" -- and which
 * the walk's own phase loops would swallow as end-of-cursor). The
 * abort is clean: fence-post snapshots destroyed on the way out.
 */
static int
test_walk_dangling_dirent(void)
{
	rt_ds_t d;
	int err;

	TEST_START("walk: dangling dirent -> EIO");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_OFFOF, &d), "hold left");
	err = rt_add_dangling_entry(d.rtd_os, d.rtd_root, "ghost");
	rt_close(&d);
	RT_CHECK(err, "add dangling entry");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EIO, "expected EIO");
	TEST_PASS();
}

/*
 * W19: hardlinked symlink and hardlinked device node, each a
 * two-member linkpool. Non-regular files carry ZPL_LINKS like any
 * other, so discovery and the completeness verify must treat them
 * identically; a clean walk completes the diff.
 */
static int
test_walk_hardlinked_specials(void)
{
	rt_ds_t d;
	uint64_t sl_obj, dv_obj;
	int err;

	TEST_START("W19: hardlinked symlink and device");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "sl",
	    "some/target", &sl_obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "sl2",
		    sl_obj);
	if (err == 0)
		err = rt_create_device(d.rtd_os, d.rtd_root, "dv",
		    0x77, &dv_obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "dv2",
		    dv_obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_OFFOF, RT_DS_ONTO, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == 0, "expected success");
	TEST_PASS();
}

void
run_walk_tests(void)
{
	(void) printf("\n[walk: union iteration, recursion, faults]\n");
	(void) test_walk_presence_matrix();
	(void) test_walk_kind_matrix();
	(void) test_walk_side_subtrees();
	(void) test_walk_deleted_dir_trees();
	(void) test_walk_deep_nesting();
	(void) test_walk_empty_filesystems();
	(void) test_walk_orphan_skipped();
	(void) test_walk_dangling_dirent();
	(void) test_walk_hardlinked_specials();
}
