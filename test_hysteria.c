// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Hysterical detection: transformations that look like something
 * happened but nothing did. Covers the nvim-style rename-on-save
 * (new dnode, same or different content) and identical changes made
 * on both sides, which cross-reference suppresses.
 */

#include "rebase_test.h"

/*
 * Rename-on-save with unchanged content: new dnode under the old
 * path, same data as base. Must not be reported as an edit.
 */
static int
test_hysterical_file(void)
{
	rt_ds_t d;
	int err;

	TEST_START("hysterical edit (nvim-style, same content)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical edit");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * Both sides edit the same file to identical content: suppressed.
 */
static int
test_suppress_both_edit_identical(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("suppress: both EDIT identical content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "same-fix\n", 9);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "same-fix\n", 9);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical edits)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides add the same path with identical content: suppressed.
 */
static int
test_suppress_both_add_identical(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("suppress: both ADD identical content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "shared",
	    "identical\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "shared",
	    "identical\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical adds)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides move the same file to the same destination and make
 * identical edits: suppressed.
 */
static int
test_suppress_both_move_edit_identical(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("suppress: both MOVE_EDIT identical");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_edit_file(d.rtd_os, obj, "moved-and-fixed\n", 16);
	rt_close(&d);
	RT_CHECK(err, "move+edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_edit_file(d.rtd_os, obj, "moved-and-fixed\n", 16);
	rt_close(&d);
	RT_CHECK(err, "move+edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical move+edit)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides rename-on-save with content unchanged from base: the
 * walker sees new obj numbers, but the content comparison says
 * nothing happened. No change recorded at all.
 */
static int
test_edge_hysterical_both_same_content(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both hysterical, same base content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (hysterical no-op)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides rename-on-save to the same NEW content: each side is
 * a real edit vs base, but cross-reference suppresses the pair as
 * identical.
 */
static int
test_edge_hysterical_both_identical_new(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both hysterical, identical new content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "new-content\n", 12, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "new-content\n", 12, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical hysterical edits)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides rename-on-save to DIFFERENT new content: a real
 * both-modified conflict through the COW boundary.
 */
static int
test_edge_hysterical_both_different(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both hysterical, different new content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "left-new\n", 9, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "right-new\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello"),
	    "expected BOTH_MODIFIED at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Delete-and-recreate on both sides with different content: same
 * mechanism as rename-on-save (new obj under the old path), and the
 * differing content makes it a both-modified conflict.
 */
static int
test_edge_delete_and_recreate(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: delete + recreate both sides (diff)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "left-recreated\n", 15, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "right-recreated\n", 16, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello"),
	    "expected BOTH_MODIFIED at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_hysteria_tests(void)
{
	(void) printf("\n[hysteria: identical-content suppression]\n");
	(void) test_hysterical_file();
	(void) test_suppress_both_edit_identical();
	(void) test_suppress_both_add_identical();
	(void) test_suppress_both_move_edit_identical();
	(void) test_edge_hysterical_both_same_content();
	(void) test_edge_hysterical_both_identical_new();
	(void) test_edge_hysterical_both_different();
	(void) test_edge_delete_and_recreate();
}
