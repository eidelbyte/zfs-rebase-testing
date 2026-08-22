// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Cross-reference: conflicts between the two changelists, each
 * conflict type triggered and verified in the manifest, plus the
 * clean-merge and benign cases that must NOT conflict.
 */

#include "rebase_test.h"

/*
 * Both sides edit the same file with different content.
 */
static int
test_conflict_both_modified(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: BOTH_MODIFIED");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "left-version\n", 13);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "right-version\n", 14);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	rt_manifest_dump(nvl);
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello"),
	    "expected BOTH_MODIFIED at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides add a file at the same path with different content.
 */
static int
test_conflict_create_create(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: CREATE_CREATE");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "newfile",
	    "left-content\n", 13, NULL);
	rt_close(&d);
	RT_CHECK(err, "create left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "newfile",
	    "right-content\n", 14, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "CREATE_CREATE",
	    "newfile"),
	    "expected CREATE_CREATE at newfile");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left edits, right deletes.
 */
static int
test_conflict_modify_delete(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: MODIFY_DELETE");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "edited-by-left\n", 15);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "delete right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MODIFY_DELETE",
	    "hello"),
	    "expected MODIFY_DELETE at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left deletes, right edits.
 */
static int
test_conflict_delete_modify(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: DELETE_MODIFY");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "delete left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "edited-by-right\n", 16);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "DELETE_MODIFY",
	    "hello"),
	    "expected DELETE_MODIFY at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left deletes "subdir" (and its contents), right adds a new file
 * inside subdir.
 */
static int
test_conflict_dir_delete_vs_edit(void)
{
	rt_ds_t d;
	uint64_t subdir;
	nvlist_t *nvl;
	int err;

	TEST_START("conflict: DIR_DELETE_VS_EDIT");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	/* Left deletes subdir/inner and subdir. */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	VERIFY0(rt_remove_entry(d.rtd_os, subdir, "inner"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "subdir"));
	rt_close(&d);

	/* Right adds a new file inside subdir. */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	err = rt_create_file(d.rtd_os, subdir, "newfile",
	    "new in subdir\n", 14, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "DIR_DELETE_VS_EDIT",
	    "subdir/newfile"),
	    "expected DIR_DELETE_VS_EDIT at subdir/newfile");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * The reverse direction: right deletes the dir, left adds inside.
 */
static int
test_edge_dir_delete_reverse(void)
{
	rt_ds_t d;
	uint64_t subdir;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: DIR_DELETE_VS_EDIT (reverse)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	/* Left adds a new file inside subdir. */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	err = rt_create_file(d.rtd_os, subdir, "leftfile",
	    "from-left\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create left");

	/* Right deletes subdir (remove inner first, then subdir). */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	VERIFY0(rt_remove_entry(d.rtd_os, subdir, "inner"));
	VERIFY0(rt_remove_entry(d.rtd_os, d.rtd_root, "subdir"));
	rt_close(&d);

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "DIR_DELETE_VS_EDIT",
	    "subdir/leftfile"),
	    "expected DIR_DELETE_VS_EDIT at subdir/leftfile");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left adds a file "foo", right adds a directory "foo": different
 * object types at the same path.
 */
static int
test_edge_file_vs_dir_same_path(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: file vs dir at same path");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "foo",
	    "file content\n", 13, NULL);
	rt_close(&d);
	RT_CHECK(err, "create file left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "foo", NULL);
	rt_close(&d);
	RT_CHECK(err, "create dir right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) >= 1,
	    "expected at least 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "CREATE_CREATE",
	    "foo"),
	    "expected CREATE_CREATE at foo");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Three different conflicts in a single rebase:
 *   "hello"        both edit differently
 *   "newfile"      both add differently
 *   "subdir/inner" left edits, right deletes
 */
static int
test_edge_multiple_conflicts(void)
{
	rt_ds_t d;
	uint64_t subdir, obj;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: multiple conflicts in one rebase");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	/* Left: edit hello, add newfile, edit subdir/inner. */
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_edit_file(d.rtd_os, obj, "left\n", 5));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "newfile",
	    "left-new\n", 9, NULL));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	VERIFY0(rt_dir_lookup(d.rtd_os, subdir, "inner", &obj));
	VERIFY0(rt_edit_file(d.rtd_os, obj, "left-inner\n", 11));
	rt_close(&d);

	/* Right: edit hello, add newfile (diff), delete subdir/inner. */
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_edit_file(d.rtd_os, obj, "right\n", 6));
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "newfile",
	    "right-new\n", 10, NULL));
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	VERIFY0(rt_remove_entry(d.rtd_os, subdir, "inner"));
	rt_close(&d);

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 3,
	    "expected 3 conflicts");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello"),
	    "expected BOTH_MODIFIED at hello");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "CREATE_CREATE",
	    "newfile"),
	    "expected CREATE_CREATE at newfile");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MODIFY_DELETE",
	    "subdir/inner"),
	    "expected MODIFY_DELETE at subdir/inner");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides delete the same file: agreement, not a conflict.
 */
static int
test_benign_both_delete(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("benign: both DELETE same file");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "delete left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "hello");
	rt_close(&d);
	RT_CHECK(err, "delete right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (both deleted)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides add files in the same directory at different paths.
 */
static int
test_clean_nonoverlapping_adds(void)
{
	rt_ds_t d;
	uint64_t subdir;
	nvlist_t *nvl;
	int err;

	TEST_START("clean: non-overlapping adds in same dir");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	err = rt_create_file(d.rtd_os, subdir, "left_new",
	    "from left\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir", &subdir));
	err = rt_create_file(d.rtd_os, subdir, "right_new",
	    "from right\n", 11, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (different paths)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Left adds a whole new subtree, right idle.
 */
static int
test_clean_left_adds_subtree(void)
{
	rt_ds_t d;
	uint64_t newdir;
	nvlist_t *nvl;
	int err;

	TEST_START("clean: left adds subtree, right unchanged");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_create_dir(d.rtd_os, d.rtd_root, "newdir", &newdir));
	err = rt_create_file(d.rtd_os, newdir, "a.txt",
	    "file a\n", 7, NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, newdir, "b.txt",
		    "file b\n", 7, NULL);
	rt_close(&d);
	RT_CHECK(err, "create subtree");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (left-only subtree)");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_crossref_tests(void)
{
	(void) printf("\n[crossref: conflicts, clean merges]\n");
	(void) test_conflict_both_modified();
	(void) test_conflict_create_create();
	(void) test_conflict_modify_delete();
	(void) test_conflict_delete_modify();
	(void) test_conflict_dir_delete_vs_edit();
	(void) test_edge_dir_delete_reverse();
	(void) test_edge_file_vs_dir_same_path();
	(void) test_edge_multiple_conflicts();
	(void) test_benign_both_delete();
	(void) test_clean_nonoverlapping_adds();
	(void) test_clean_left_adds_subtree();
}
