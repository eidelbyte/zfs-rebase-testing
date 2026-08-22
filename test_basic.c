// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Basic diff coverage: the recursive walk, single-side standalone
 * changes, changelist counts, and the entry-point error cases.
 * Tests that end in a bare dsl_rebase(..., NULL) only assert the
 * ENOSYS success sentinel (the apply phase is unimplemented);
 * manifest-inspecting tests use rt_run_rebase().
 */

#include "rebase_test.h"

/*
 * Smoke: no changes on either side. Diff should produce empty
 * changelists and every later phase should be a no-op.
 */
static int
test_smoke_no_changes(void)
{
	int err;

	TEST_START("smoke: no changes on either side");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS,
	    "expected ENOSYS from successful diff+collapse");
	TEST_PASS();
}

static int
test_left_add(void)
{
	rt_ds_t d;
	int err;

	TEST_START("left adds a file, right unchanged");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "newfile",
	    "added\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "create newfile");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

static int
test_right_add(void)
{
	rt_ds_t d;
	int err;

	TEST_START("right adds a file, left unchanged");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "rightfile",
	    "from-right\n", 11, NULL);
	rt_close(&d);
	RT_CHECK(err, "create rightfile");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

static int
test_left_delete(void)
{
	rt_ds_t d;
	int err;

	TEST_START("left deletes a file");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "remove hello");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Edit in place: same dnode, new content (COW rewrites the blocks
 * but the obj number is stable).
 */
static int
test_left_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("left edits a file in-place");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "edited\n", 7);
	rt_close(&d);
	RT_CHECK(err, "edit hello");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

static int
test_both_add_different(void)
{
	rt_ds_t d;
	int err;

	TEST_START("both sides add different files");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "leftfile",
	    "left\n", 5, NULL);
	rt_close(&d);
	RT_CHECK(err, "create leftfile");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "rightfile",
	    "right\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "create rightfile");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Both sides edit the same file with different content. The diff
 * phase must record an EDIT on each side; the resulting conflict
 * is asserted separately in the crossref section.
 */
static int
test_both_edit(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("both sides edit same file");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "left-edit\n", 10);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "right-edit\n", 11);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

static int
test_nested_edit(void)
{
	rt_ds_t d;
	uint64_t subdir, obj;
	int err;

	TEST_START("nested: edit file inside subdirectory");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	VERIFY0(rt_dir_lookup(d.rtd_os, subdir, "inner", &obj));
	err = rt_edit_file(d.rtd_os, obj, "modified-inner\n", 15);
	rt_close(&d);
	RT_CHECK(err, "edit inner");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Add, delete, and edit all in one changelist.
 */
static int
test_mixed_operations(void)
{
	rt_ds_t d;
	uint64_t subdir, obj;
	int err;

	TEST_START("mixed: add + delete + edit on left");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "brand_new",
	    "new content\n", 12, NULL);
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	if (err == 0) {
		VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir",
		    &subdir));
		VERIFY0(rt_dir_lookup(d.rtd_os, subdir, "inner", &obj));
		err = rt_edit_file(d.rtd_os, obj, "modified\n", 9);
	}
	rt_close(&d);
	RT_CHECK(err, "mutate left");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Both sides edit a deeply nested file. Verifies the recursive ZAP
 * walker builds full paths correctly.
 */
static int
test_edge_deep_nested_both_edit(void)
{
	rt_ds_t d;
	uint64_t d1, d2, obj;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: deep nested both edit");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	/* Create a/b/c.txt in src. */
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "a", &d1));
	VERIFY0(rt_create_dir(d.rtd_os, d1, "b", &d2));
	VERIFY0(rt_create_file(d.rtd_os, d2, "c.txt", "deep\n", 5, NULL));
	rt_close(&d);

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "a", &d1));
	VERIFY0(rt_dir_lookup(d.rtd_os, d1, "b", &d2));
	VERIFY0(rt_dir_lookup(d.rtd_os, d2, "c.txt", &obj));
	err = rt_edit_file(d.rtd_os, obj, "left-deep\n", 10);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "a", &d1));
	VERIFY0(rt_dir_lookup(d.rtd_os, d1, "b", &d2));
	VERIFY0(rt_dir_lookup(d.rtd_os, d2, "c.txt", &obj));
	err = rt_edit_file(d.rtd_os, obj, "right-deep\n", 11);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "a/b/c.txt"),
	    "expected BOTH_MODIFIED at a/b/c.txt");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Changelist counts reach the manifest: left adds 2 files, right
 * adds 1.
 */
static int
test_edge_changelist_counts(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: changelist counts in manifest");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "l1",
	    "one\n", 4, NULL));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "l2",
	    "two\n", 4, NULL));
	rt_close(&d);

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "r1",
	    "three\n", 6, NULL));
	rt_close(&d);

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	rt_manifest_dump(nvl);
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts");
	TEST_EXPECT(rt_manifest_left_nchanges(nvl) >= 2,
	    "expected left_nchanges >= 2");
	TEST_EXPECT(rt_manifest_right_nchanges(nvl) >= 1,
	    "expected right_nchanges >= 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

static int
test_error_same_dataset(void)
{
	int err;

	TEST_START("error: left == right (same dataset)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	err = dsl_rebase(RT_DS_LEFT, RT_DS_LEFT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EINVAL, "expected EINVAL");
	TEST_PASS();
}

static int
test_error_left_is_snapshot(void)
{
	int err;

	TEST_START("error: left is a snapshot");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_snapshot(RT_DS_LEFT, "snap1"), "snapshot left");

	err = dsl_rebase(RT_DS_LEFT "@snap1", RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EINVAL, "expected EINVAL");
	TEST_PASS();
}

void
run_basic_tests(void)
{
	(void) printf("\n[basic: walk, standalone diff, errors]\n");
	(void) test_smoke_no_changes();
	(void) test_left_add();
	(void) test_right_add();
	(void) test_left_delete();
	(void) test_left_edit();
	(void) test_both_add_different();
	(void) test_both_edit();
	(void) test_nested_edit();
	(void) test_mixed_operations();
	(void) test_edge_deep_nested_both_edit();
	(void) test_edge_changelist_counts();
	(void) test_error_same_dataset();
	(void) test_error_left_is_snapshot();
}
