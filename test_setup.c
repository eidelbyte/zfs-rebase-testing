// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Setup-phase matrix: ancestor discovery, preconditions, and the
 * fence-post snapshots. Cells S1-S16 in TEST-MATRIX.md; each test
 * names the cells it covers. S1 (the all-defaults happy path) is
 * covered by every smoke test in the basic section; S3/S4 (left is
 * a snapshot, left == right) live in test_basic.c's error tests.
 */

#include "rebase_test.h"

/*
 * S2: right passed as a snapshot. No @%rebase-right-snap is
 * created (a snapshot argument is already immutable); ancestor
 * discovery walks the snapshot's own prev chain to the base.
 */
static int
test_setup_right_as_snapshot(void)
{
	int err;

	TEST_START("setup: right given as a snapshot");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_snapshot(RT_DS_RIGHT, "rsnap"), "snapshot right");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT "@rsnap", NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * S5: right is a snapshot that already appears in the left chain
 * (the base itself). History is already linear -- nothing to
 * rebase.
 */
static int
test_setup_linear_history(void)
{
	int err;

	TEST_START("setup: right snapshot in left chain");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	err = dsl_rebase(RT_DS_LEFT, RT_DS_SRC "@base", NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EINVAL, "expected EINVAL (linear history)");
	TEST_PASS();
}

/*
 * S6: unrelated datasets -- right shares no snapshot history with
 * left, so no common ancestor exists.
 */
static int
test_setup_unrelated_datasets(void)
{
	int err;

	TEST_START("setup: unrelated datasets");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_create_zpl_dataset(POOL_NAME "/stranger"),
	    "create stranger");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, POOL_NAME "/stranger", NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOENT, "expected ENOENT (no ancestor)");
	TEST_PASS();
}

/*
 * S7: a leftover @%rebase-snap from a crashed run. Fence-post
 * creation must fail EEXIST (recovery is the abort path's future
 * business), after preconditions passed.
 */
static int
test_setup_leftover_fence_snap(void)
{
	int err;

	TEST_START("setup: leftover %rebase-snap -> EEXIST");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_snapshot(RT_DS_LEFT, "%rebase-snap"),
	    "pre-create fence snap");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EEXIST, "expected EEXIST");
	TEST_PASS();
}

/*
 * S8: name-semantics property differs between the sides (test 14
 * of the planning-doc catalog). The clones inherit identical
 * MASTER_NODE content, so the mismatch is injected on one clone
 * after the fact.
 */
static int
test_setup_props_mismatch(void)
{
	rt_ds_t d;
	int err;

	TEST_START("setup: casesensitivity mismatch");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_set_zplprop(d.rtd_os, "casesensitivity",
	    ZFS_CASE_INSENSITIVE);
	rt_close(&d);
	RT_CHECK(err, "set casesensitivity");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOTSUP, "expected ENOTSUP");
	TEST_PASS();
}

/*
 * S9: all three datasets AGREE on casesensitivity=insensitive.
 * Equality is not enough: byte-exact name matching requires
 * sensitive, so v1 rejects it anyway.
 */
static int
test_setup_case_insensitive_rejected(void)
{
	rt_ds_t d;
	int err;

	TEST_START("setup: agreeing case-insensitive rejected");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	VERIFY0(rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL));
	err = rt_set_zplprop(d.rtd_os, "casesensitivity",
	    ZFS_CASE_INSENSITIVE);
	rt_close(&d);
	RT_CHECK(err, "set casesensitivity");

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOTSUP, "expected ENOTSUP");
	TEST_PASS();
}

/*
 * S10: all three agree on a normalization setting. Same story as
 * S9: normalizing datasets can store one logical name as different
 * bytes, so v1 rejects them.
 */
static int
test_setup_normalization_rejected(void)
{
	rt_ds_t d;
	int err;

	TEST_START("setup: agreeing normalization rejected");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_set_zplprop(d.rtd_os, "normalization", 1);
	rt_close(&d);
	RT_CHECK(err, "set normalization");

	RT_CHECK(rt_scaffold_snap_and_clone(), "snap/clone failed");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOTSUP, "expected ENOTSUP");
	TEST_PASS();
}

/*
 * S11: nonexistent left dataset.
 */
static int
test_setup_missing_left(void)
{
	int err;

	TEST_START("setup: nonexistent left dataset");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	err = dsl_rebase(POOL_NAME "/nosuch", RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOENT, "expected ENOENT");
	TEST_PASS();
}

/*
 * S12: an active scrub must reject the rebase with EBUSY. A scrub
 * on a near-empty 128M pool can finish between transactions, so
 * the fixture PAUSES it -- dsl_scan_active() still counts a paused
 * scrub, and the pause pins the state deterministically. The
 * start-then-pause pair retries because the scrub can complete in
 * the gap (pause then returns ENOENT).
 */
static int
test_setup_scrub_busy(void)
{
	spa_t *spa;
	int err, perr;

	TEST_START("setup: active (paused) scrub");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(spa_open(POOL_NAME, &spa, FTAG), "open spa");
	perr = ENOENT;
	for (int try = 0; try < 5 && perr != 0; try++) {
		err = spa_scan(spa, POOL_SCAN_SCRUB);
		if (err != 0 && err != EBUSY)
			break;
		perr = dsl_scrub_set_pause_resume(spa_get_dsl(spa),
		    POOL_SCRUB_PAUSE);
	}
	spa_close(spa, FTAG);
	RT_CHECK(perr, "could not hold a scrub open");

	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == EBUSY, "expected EBUSY");
	TEST_PASS();
}

/*
 * S14: a zvol as a side. The lineage is built entirely of zvols
 * (a zvol head and its clone) so ancestor discovery FINDS a common
 * base and the all-ZPL precondition is what fires -- an unrelated
 * zvol would die earlier with ENOENT in discovery. The empty zvol
 * objset never trips the master-node reads because the type check
 * runs first, in pass 1, before any fence snapshot exists.
 */
static int
test_setup_zvol_side(void)
{
	int err;

	TEST_START("setup: zvol lineage rejected");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_create_zvol_dataset(POOL_NAME "/zsrc"),
	    "create zvol");
	rt_sync_pool();
	RT_CHECK(rt_snapshot(POOL_NAME "/zsrc", "zbase"),
	    "snapshot zvol");
	RT_CHECK(rt_clone(POOL_NAME "/zclone",
	    POOL_NAME "/zsrc@zbase"), "clone zvol");

	rt_sync_pool();
	err = dsl_rebase(POOL_NAME "/zsrc", POOL_NAME "/zclone", NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOTSUP, "expected ENOTSUP");
	TEST_PASS();
}

/*
 * S15: ZPL version below SA support on one side. The version is
 * read through zfs_get_zplprop from the MASTER_NODE key, the same
 * store the case/normalization cells exercise.
 */
static int
test_setup_zpl_version_low(void)
{
	rt_ds_t d;
	int err;

	TEST_START("setup: ZPL version < SA rejected");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_set_zplprop(d.rtd_os, ZPL_VERSION_STR,
	    ZPL_VERSION_SA - 1);
	rt_close(&d);
	RT_CHECK(err, "downgrade version");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOTSUP, "expected ENOTSUP");
	TEST_PASS();
}

/*
 * S16: FUID table object mismatch across the three read sources.
 * Any differing value does -- the check compares the MASTER_NODE
 * key, not the table contents.
 */
static int
test_setup_fuid_mismatch(void)
{
	rt_ds_t d;
	int err;

	TEST_START("setup: FUID table mismatch");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_set_zplprop(d.rtd_os, ZFS_FUID_TABLES, 12345);
	rt_close(&d);
	RT_CHECK(err, "inject FUID key");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_scaffold_teardown();

	TEST_EXPECT(err == ENOTSUP, "expected ENOTSUP");
	TEST_PASS();
}

void
run_setup_tests(void)
{
	(void) printf("\n[setup: discovery, preconditions, fences]\n");
	(void) test_setup_right_as_snapshot();
	(void) test_setup_linear_history();
	(void) test_setup_unrelated_datasets();
	(void) test_setup_leftover_fence_snap();
	(void) test_setup_props_mismatch();
	(void) test_setup_case_insensitive_rejected();
	(void) test_setup_normalization_rejected();
	(void) test_setup_missing_left();
	(void) test_setup_scrub_busy();
	(void) test_setup_zvol_side();
	(void) test_setup_zpl_version_low();
	(void) test_setup_fuid_mismatch();
}
