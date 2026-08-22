// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Hardlink cases. A linkpool is the set of paths all referencing
 * the same dnode through hardlinks; in the sprint-2 two-axis model
 * these operations produce linkpool-axis records independent of the
 * content axis. The assertions below only inspect the manifest, so
 * they hold across both the sprint-1 and sprint-2 engines.
 *
 * The sprint-2 14-test catalog (sever-vs-nothing, phantom-conflict
 * dissolution, index recycling, linkpool completeness VERIFY, novel
 * overlap, ...) lands in this file with the testing-framework issue.
 * Note that rt_add_hardlink() does not yet bump ZPL_LINKS, which the
 * sprint-2 walker reads to build linkpool tables; fix the helper
 * before wiring up those tests.
 */

#include "rebase_test.h"

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
	(void) printf("\n[linkpool: hardlinks]\n");
	(void) test_left_hardlink();
	(void) test_edge_hardlink_edit_both_sides();
	(void) test_edge_hardlink_delete_no_conflict();
	(void) test_edge_hardlink_delete_vs_edit();
}
