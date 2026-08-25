// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Cross-domain seam matrix (X) tests -- see TEST-MATRIX.md,
 * "Cross-domain seam matrix". Each test's comment names the cell
 * it covers. Planning doc: membership-content-interface.md.
 *
 * The shape under test: one side holds a linkpool destination at a
 * path, the other side deferred with SAME_AS_BASE while holding a
 * content-bearing record there whose dnode is foreign to the
 * destination's lineage. Before the fix such rows resolved to the
 * pool silently; now the seam consultation in merge_row compares
 * the two files and conflicts when they differ (CREATE_CREATE for
 * an ADD claim, MOVE_VS_EDIT for a move destination, BOTH_MODIFIED
 * for an EDIT claim), leaving the row undecided. Convergent
 * content still defers, on-lineage claims still defer (the grow
 * case and the base-pool ground), and the widened GONE synthesis
 * makes every collapsed move's old path contest pool claims as
 * DELETE_VS_RELINK.
 *
 * Observed through the manifest (typed conflicts) plus the finals
 * and conflicts summary lines: a consultation conflict lands in
 * the finals conflict bucket (its row stays undecided) and in the
 * conflicts total but in none of the four membership buckets.
 */

#include "rebase_test.h"

/*
 * Scrape the targets/finals/conflicts lines and check the
 * row-count invariant (finals buckets must sum to the row count).
 * Returns nonzero if the lines are missing or the invariant is
 * broken.
 */
static int
xs_stats(rt_final_stats_t *fs, rt_conflict_stats_t *cs)
{
	rt_target_stats_t ts;
	uint64_t rows = 0, sumf = 0;
	int err, i;

	err = rt_target_stats(&ts);
	if (err == 0)
		err = rt_final_stats(fs);
	if (err == 0)
		err = rt_conflict_stats(cs);
	if (err != 0)
		return (err);

	for (i = 0; i < 6; i++)
		rows += ts.rts_left[i];
	for (i = 0; i < 7; i++)
		sumf += fs->rfs_kind[i];
	if (rows != sumf) {
		(void) printf("\n    [xs] row-count invariant broken: "
		    "rows %llu != finals sum %llu\n",
		    (unsigned long long)rows,
		    (unsigned long long)sumf);
		return (-1);
	}
	return (0);
}

/*
 * X1: left creates a plain file at a new name; right creates the
 * same name as a member of a novel two-name pool with different
 * bytes. Pre-fix the pool won silently and the compiled LINK would
 * have clobbered left's file; now the claim is heard and the
 * collision is CREATE_CREATE with the row undecided. The undecided
 * row compiles nothing at P, but the rest of the pool proceeds:
 * the right-won group compiles its content WRITE, and the
 * uncontested member's decided row compiles its LINK -- exactly
 * two actions, both safe under either phase-2 resolution of P.
 * (Two box runs calibrated this pin: row-driven and group-driven
 * actions are guarded by row state and group source, not by
 * conflict coverage -- coverage guards the record-driven
 * standalone sweep.)
 */
static int
test_seam_create_vs_pool(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X1: standalone create vs pool create");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "mine", 4,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "left create");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "theirs", 6,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P", obj);
	rt_close(&d);
	RT_CHECK(err, "right pool create");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "CREATE_CREATE",
	    "P"), "expected CREATE_CREATE at P");
	TEST_EXPECT(fs.rfs_kind[6] == 1 && fs.rfs_kind[5] == 1,
	    "expected finals novel 1 + conflict 1");
	TEST_EXPECT(rt_manifest_nactions(nvl) == 2,
	    "expected WRITE + LINK for the uncontested remainder");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X2: X1 with the sides swapped -- the consultation is symmetric
 * in polarity.
 */
static int
test_seam_create_vs_pool_mirror(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X2: pool create vs standalone create (mirror)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "mine", 4,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P", obj);
	rt_close(&d);
	RT_CHECK(err, "left pool create");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "theirs", 6,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "right create");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "CREATE_CREATE",
	    "P"), "expected CREATE_CREATE at P");
	TEST_EXPECT(fs.rfs_kind[6] == 1 && fs.rfs_kind[5] == 1,
	    "expected finals novel 1 + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X3: convergent create -- left's plain file carries exactly the
 * bytes of the base file right linked the name to. Identical
 * content is convergence: no conflict, and the name joins the
 * pool (the expressed sharing stands, the content is satisfied).
 */
static int
test_seam_create_convergent(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X3: convergent create defers into the pool");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "M", "vv", 2,
	    &obj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "vv", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "left create");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P", obj);
	rt_close(&d);
	RT_CHECK(err, "right link");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected zero conflicts (convergent)");
	TEST_EXPECT(fs.rfs_kind[3] == 2 && fs.rfs_kind[6] == 0,
	    "expected finals anchor 2, no undecided rows");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X4: base standalone M; left edits M in place; right repoints the
 * name into a pool around a foreign base file. Pre-fix left's edit
 * was silently discarded; now it is BOTH_MODIFIED.
 */
static int
test_seam_edit_vs_repoint(void)
{
	rt_ds_t d;
	uint64_t mobj, qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X4: in-place edit vs repoint into a pool");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "q1",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_edit_file(d.rtd_os, mobj, "m2", 2);
	rt_close(&d);
	RT_CHECK(err, "left edit");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "P");
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P",
		    qobj);
	rt_close(&d);
	RT_CHECK(err, "right repoint");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "P"), "expected BOTH_MODIFIED at P");
	TEST_EXPECT(fs.rfs_kind[3] == 1 && fs.rfs_kind[6] == 1,
	    "expected finals anchor 1 + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X5: replace flavor -- left replaces the name with a brand-new
 * dnode; right GROWS a pool around base M keeping the name.
 * Growing is only safe for on-lineage claims, and the replace
 * swapped the dnode out from under the grow: BOTH_MODIFIED.
 */
static int
test_seam_replace_vs_grow(void)
{
	rt_ds_t d;
	uint64_t mobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X5: replace vs grow around the old dnode");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "P", "L2", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "left replace");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P2", mobj);
	rt_close(&d);
	RT_CHECK(err, "right grow");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "P"), "expected BOTH_MODIFIED at P");
	TEST_EXPECT(fs.rfs_kind[3] == 1 && fs.rfs_kind[6] == 1,
	    "expected finals anchor 1 + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X6: X4 with the sides swapped.
 */
static int
test_seam_edit_vs_repoint_mirror(void)
{
	rt_ds_t d;
	uint64_t mobj, qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X6: repoint vs in-place edit (mirror)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "q1",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "P");
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P",
		    qobj);
	rt_close(&d);
	RT_CHECK(err, "left repoint");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_edit_file(d.rtd_os, mobj, "m2", 2);
	rt_close(&d);
	RT_CHECK(err, "right edit");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "P"), "expected BOTH_MODIFIED at P");
	TEST_EXPECT(fs.rfs_kind[3] == 1 && fs.rfs_kind[6] == 1,
	    "expected finals anchor 1 + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X7: convergent edit -- left edits M to exactly the bytes of the
 * pool file right repointed the name to. Convergence defers and
 * the name joins the pool cleanly.
 */
static int
test_seam_edit_convergent(void)
{
	rt_ds_t d;
	uint64_t mobj, qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X7: convergent edit defers into the pool");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "vv",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_edit_file(d.rtd_os, mobj, "vv", 2);
	rt_close(&d);
	RT_CHECK(err, "left edit");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "P");
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P",
		    qobj);
	rt_close(&d);
	RT_CHECK(err, "right repoint");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected zero conflicts (convergent)");
	TEST_EXPECT(fs.rfs_kind[3] == 2 && fs.rfs_kind[6] == 0,
	    "expected finals anchor 2, no undecided rows");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X8: move source vs grow. Left renames the standalone name away;
 * right grows a pool around the same dnode keeping the old name.
 * The widened GONE synthesis gives the old path a real row, and
 * the existing GONE-vs-destination arm fires DELETE_VS_RELINK.
 * Pre-fix the old path had no row and the pool kept a name the
 * rename deleted, silently.
 */
static int
test_seam_move_vs_grow(void)
{
	rt_ds_t d;
	uint64_t mobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X8: move source vs pool grown at old name");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "left rename");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P3", mobj);
	rt_close(&d);
	RT_CHECK(err, "right grow");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl,
	    "DELETE_VS_RELINK", "P"),
	    "expected DELETE_VS_RELINK at old path P");
	TEST_EXPECT(cs.rcs_relink == 1,
	    "expected the relink bucket to count it");
	TEST_EXPECT(fs.rfs_kind[6] == 1,
	    "expected one undecided row");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X9: move source vs repoint into a BASE pool -- the winning
 * destination is an anchored member pool, and the conflict is
 * keyed by its lineage.
 */
static int
test_seam_move_vs_repoint(void)
{
	rt_ds_t d;
	uint64_t nobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X9: move source vs repoint into base pool");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "n1", 2,
	    &nobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B",
		    nobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1",
		    2, NULL);
	rt_close(&d);
	RT_CHECK(err, "base pool + file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "left rename");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "P");
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P",
		    nobj);
	rt_close(&d);
	RT_CHECK(err, "right repoint");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl,
	    "DELETE_VS_RELINK", "P"),
	    "expected DELETE_VS_RELINK at old path P");
	TEST_EXPECT(cs.rcs_relink == 1,
	    "expected the relink bucket to count it");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X10: X8 with the sides swapped.
 */
static int
test_seam_move_vs_grow_mirror(void)
{
	rt_ds_t d;
	uint64_t mobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X10: pool at old name vs move source (mirror)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P3", mobj);
	rt_close(&d);
	RT_CHECK(err, "left grow");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "right rename");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl,
	    "DELETE_VS_RELINK", "P"),
	    "expected DELETE_VS_RELINK at old path P");
	TEST_EXPECT(cs.rcs_relink == 1,
	    "expected the relink bucket to count it");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X11: move destination vs pool claim. Left renames M onto a new
 * name; right independently links that name into a pool with
 * different bytes. The MOVE record is a content claim at its new
 * path, and its dnode is foreign to the pool: MOVE_VS_EDIT.
 */
static int
test_seam_move_dest_vs_pool(void)
{
	rt_ds_t d;
	uint64_t qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X11: move destination vs pool claim");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "q1",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "left rename");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P2", qobj);
	rt_close(&d);
	RT_CHECK(err, "right link at destination");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "P2"), "expected MOVE_VS_EDIT at destination P2");
	TEST_EXPECT(fs.rfs_kind[1] == 1 && fs.rfs_kind[6] == 1,
	    "expected finals gone 1 (old path) + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X12: X11 with the sides swapped.
 */
static int
test_seam_move_dest_vs_pool_mirror(void)
{
	rt_ds_t d;
	uint64_t qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X12: pool claim vs move destination (mirror)");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "q1",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P2", qobj);
	rt_close(&d);
	RT_CHECK(err, "left link at destination");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "right rename");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "MOVE_VS_EDIT",
	    "P2"), "expected MOVE_VS_EDIT at destination P2");
	TEST_EXPECT(fs.rfs_kind[1] == 1 && fs.rfs_kind[6] == 1,
	    "expected finals gone 1 (old path) + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X13: convergent move destination -- the pool's bytes equal the
 * moved file's bytes, so the destination joins the pool cleanly
 * and the old path goes quietly GONE.
 */
static int
test_seam_move_dest_convergent(void)
{
	rt_ds_t d;
	uint64_t qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X13: convergent move destination defers");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "vv", 2,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "vv",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "left rename");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P2", qobj);
	rt_close(&d);
	RT_CHECK(err, "right link at destination");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected zero conflicts (convergent)");
	TEST_EXPECT(fs.rfs_kind[1] == 1 && fs.rfs_kind[3] == 2 &&
	    fs.rfs_kind[6] == 0,
	    "expected finals gone 1, anchor 2, no undecided rows");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X14: a pool grown around a moved file (the doc's X-E). Left
 * MOVE_EDITs M away from its name; right grows a pool around M
 * keeping that name. The tension surfaces exactly once, at the
 * old path (DELETE_VS_RELINK); the new path pairs SAME with SAME
 * and produces no MOVE_VS_EDIT or BOTH_MODIFIED noise -- the
 * lineage is shared, so content is the group's business.
 */
static int
test_seam_pool_around_moved(void)
{
	rt_ds_t d;
	uint64_t mobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X14: pool grown around a moved dnode");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	if (err == 0)
		err = rt_edit_file(d.rtd_os, mobj, "m2", 2);
	rt_close(&d);
	RT_CHECK(err, "left move+edit");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P3", mobj);
	rt_close(&d);
	RT_CHECK(err, "right grow");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly ONE conflict (old path owns it)");
	TEST_EXPECT(rt_manifest_has_conflict(nvl,
	    "DELETE_VS_RELINK", "P"),
	    "expected DELETE_VS_RELINK at old path P");
	TEST_EXPECT(cs.rcs_relink == 1 && cs.rcs_total == 1,
	    "expected no content-conflict noise");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X15 control: edit versus grow (the doc's X-CTL-GROW). Left edits
 * M in place; right grows a pool around M. The claim's dnode IS
 * the destination lineage, so it defers and the group owns the
 * content -- this worked before the fix and must keep working.
 */
static int
test_seam_ctl_grow(void)
{
	rt_ds_t d;
	uint64_t mobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X15: control -- edit vs grow stays clean");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    &mobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_edit_file(d.rtd_os, mobj, "m2", 2);
	rt_close(&d);
	RT_CHECK(err, "left edit");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P3", mobj);
	rt_close(&d);
	RT_CHECK(err, "right grow");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected zero conflicts (grow is on-lineage)");
	TEST_EXPECT(fs.rfs_kind[3] == 2 && fs.rfs_kind[6] == 0,
	    "expected finals anchor 2, no undecided rows");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X16 control: dual against dual (the doc's X-CTL-DUAL). Both
 * sides link the same new name into their own novel pool with
 * different bytes. Dual records can never defer -- each non-NONE
 * linkpool op forces a real target -- so the row is a both-NOVEL
 * contest and phase C's overlap conflict fires. Pins the
 * dual-record lemma at the seam.
 */
static int
test_seam_ctl_dual(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X16: control -- dual vs dual contests");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "Z1", "aa", 2,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P", obj);
	rt_close(&d);
	RT_CHECK(err, "left pool");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "Z2", "bb", 2,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P", obj);
	rt_close(&d);
	RT_CHECK(err, "right pool");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl,
	    "NOVEL_LINKPOOL_OVERLAP", "P"),
	    "expected NOVEL_LINKPOOL_OVERLAP at P");
	TEST_EXPECT(fs.rfs_kind[5] == 2 && fs.rfs_kind[6] == 1,
	    "expected finals novel 2 + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X17 control: the base-lineage exemption (the doc's
 * X-CTL-BASEPOOL). Base pool N {A,A2,P}; left edits N's shared
 * file; right repoints P into a pool around a foreign base file.
 * Left's claim at P rides the BASE lineage, which the group merge
 * and dead-pool scan already own, so the consultation must let it
 * defer: zero conflicts, and the membership change wins the row.
 * A consultation missing the exemption would fire a false
 * BOTH_MODIFIED here. (Three names, not two, so the right-side
 * remnant {A,A2} is still a pool and N stays alive -- a two-name
 * fixture would fold in the dead-pool rules and muddy the
 * control.)
 */
static int
test_seam_ctl_basepool(void)
{
	rt_ds_t d;
	uint64_t nobj, qobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X17: control -- base-pool edit vs repoint");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "n1", 2,
	    &nobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "A2",
		    nobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P",
		    nobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "Q", "q1",
		    2, &qobj);
	rt_close(&d);
	RT_CHECK(err, "base pool + file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_edit_file(d.rtd_os, nobj, "n2", 2);
	rt_close(&d);
	RT_CHECK(err, "left pool edit");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "P");
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P",
		    qobj);
	rt_close(&d);
	RT_CHECK(err, "right repoint");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected zero conflicts (base-lineage exemption)");
	TEST_EXPECT(fs.rfs_kind[0] == 2 && fs.rfs_kind[3] == 2 &&
	    fs.rfs_kind[6] == 0,
	    "expected finals same 2, anchor 2, no undecided rows");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X18 regression: a plain uncontested standalone rename. The
 * widened synthesis now gives the old path a GONE row (finals gone
 * 1) while the diff-level tuples stay exactly M1's: one left move,
 * no move-edits, one left change, and nothing conflicts or
 * compiles actions.
 */
static int
test_seam_reg_standalone_move(void)
{
	rt_ds_t d;
	rt_move_stats_t ms;
	uint64_t lcount, rcount;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr, merr;

	TEST_START("X18: regression -- lone standalone rename");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P", "m1", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "P", d.rtd_root,
	    "P2");
	rt_close(&d);
	RT_CHECK(err, "left rename");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	merr = (err == 0) ? rt_move_stats(&ms) : 0;
	if (merr == 0 && err == 0)
		merr = rt_changelist_counts(&lcount, &rcount);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0 && merr == 0, "summary lines");
	TEST_EXPECT(ms.rms_moves_left == 1 &&
	    ms.rms_move_edits_left == 0 && lcount == 1 &&
	    rcount == 0, "expected the M1 diff tuple unchanged");
	TEST_EXPECT(fs.rfs_kind[0] == 1 && fs.rfs_kind[1] == 1 &&
	    fs.rfs_kind[6] == 0,
	    "expected finals same 1 (dest) + gone 1 (old path)");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0 &&
	    rt_manifest_nactions(nvl) == 0,
	    "expected no conflicts, no actions");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * X19: a FRAGMENT winner. Right splits {B,C} out of base pool N
 * onto a new shared dnode and links a brand-new name P4 to it;
 * left independently creates P4 with different bytes. The
 * consultation's on-lineage test uses the fragment's PARENT (a
 * base-rooted number) and its data compare reads the fragment's
 * own dnode from the side table: foreign claim, CREATE_CREATE.
 */
static int
test_seam_fragment_winner(void)
{
	rt_ds_t d;
	uint64_t nobj, zobj;
	nvlist_t *nvl;
	rt_final_stats_t fs;
	rt_conflict_stats_t cs;
	int err, serr;

	TEST_START("X19: create vs fragment member at new name");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "n1", 2,
	    &nobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B",
		    nobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "C",
		    nobj);
	rt_close(&d);
	RT_CHECK(err, "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "P4", "L1", 2,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "left create");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "B");
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "C");
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "B", "z1",
		    2, &zobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "C",
		    zobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "P4",
		    zobj);
	rt_close(&d);
	RT_CHECK(err, "right fragment + new name");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	serr = (err == 0) ? xs_stats(&fs, &cs) : 0;
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(serr == 0, "summary lines");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected exactly one conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "CREATE_CREATE",
	    "P4"), "expected CREATE_CREATE at P4");
	TEST_EXPECT(fs.rfs_kind[4] == 2 && fs.rfs_kind[6] == 1,
	    "expected finals fragment 2 + conflict 1");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_seam_tests(void)
{
	(void) printf("\n[seam: cross-domain consultation (X matrix)]\n");
	(void) test_seam_create_vs_pool();
	(void) test_seam_create_vs_pool_mirror();
	(void) test_seam_create_convergent();
	(void) test_seam_edit_vs_repoint();
	(void) test_seam_replace_vs_grow();
	(void) test_seam_edit_vs_repoint_mirror();
	(void) test_seam_edit_convergent();
	(void) test_seam_move_vs_grow();
	(void) test_seam_move_vs_repoint();
	(void) test_seam_move_vs_grow_mirror();
	(void) test_seam_move_dest_vs_pool();
	(void) test_seam_move_dest_vs_pool_mirror();
	(void) test_seam_move_dest_convergent();
	(void) test_seam_pool_around_moved();
	(void) test_seam_ctl_grow();
	(void) test_seam_ctl_basepool();
	(void) test_seam_ctl_dual();
	(void) test_seam_reg_standalone_move();
	(void) test_seam_fragment_winner();
}
