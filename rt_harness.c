// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Harness plumbing: test counters, dataset handles, pool sync, the
 * rebase runner, and manifest inspection helpers.
 */

#include "rebase_test.h"

int rt_tests_run;
int rt_tests_passed;
int rt_tests_failed;

/*
 * Own a dataset, set up SA, and look up its root directory object.
 * The rt_ds_t address is the ownership tag, so the same struct must
 * be handed to rt_close().
 */
int
rt_open(const char *dsname, rt_ds_t *ds)
{
	sa_attr_type_t *tbl;
	int err;

	err = dmu_objset_own(dsname, DMU_OST_ZFS, B_FALSE, B_FALSE,
	    ds, &ds->rtd_os);
	if (err != 0)
		return (err);

	err = rt_sa_setup(ds->rtd_os, &tbl);
	if (err == 0)
		err = zap_lookup(ds->rtd_os, MASTER_NODE_OBJ,
		    ZFS_ROOT_OBJ, 8, 1, &ds->rtd_root);
	if (err != 0) {
		dmu_objset_disown(ds->rtd_os, B_FALSE, ds);
		ds->rtd_os = NULL;
		return (err);
	}

	return (0);
}

void
rt_close(rt_ds_t *ds)
{
	dmu_objset_disown(ds->rtd_os, B_FALSE, ds);
	ds->rtd_os = NULL;
}

/*
 * Force all pending transactions to disk. Must be called before
 * snapshotting or running rebase so the on-disk state is consistent.
 */
void
rt_sync_pool(void)
{
	spa_t *spa;

	VERIFY0(spa_open(POOL_NAME, &spa, FTAG));
	txg_wait_synced(spa_get_dsl(spa), 0);
	spa_close(spa, FTAG);
}

/*
 * Run dsl_rebase left onto right with manifest output. ENOSYS is
 * the success sentinel while the apply phase is unimplemented.
 * Allocates *nvlp on success; caller must fnvlist_free() it.
 */
int
rt_run_rebase(nvlist_t **nvlp)
{
	nvlist_t *nvl;
	int err;

	nvl = fnvlist_alloc();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, nvl);
	if (err == 0) {
		*nvlp = nvl;
		return (0);
	}
	fnvlist_free(nvl);
	rt_rebase_diag();
	return (err);
}


/*
 * Find the LAST dbgmsg-ring line containing `needle` and copy it
 * (from the needle onward) into line_out. libzpool records dbgmsgs
 * unconditionally (zfs_dbgmsg_enable defaults on) and
 * zfs_dbgmsg_print() dumps the whole ring to any fd; we write it
 * to a temp file and keep the last match, so earlier runs in the
 * same process cannot bleed in. Returns ENOENT when no line
 * matches.
 */
static int
rt_dbgmsg_last(const char *needle, char *line_out, size_t outlen)
{
	char tmpl[] = "/tmp/rtest_dbgmsg.XXXXXX";
	char line[512];
	FILE *f;
	int fd;
	boolean_t found = B_FALSE;

	fd = mkstemp(tmpl);
	if (fd < 0)
		return (errno);

	zfs_dbgmsg_print(fd, "rt");
	(void) lseek(fd, 0, SEEK_SET);

	f = fdopen(fd, "r");
	if (f == NULL) {
		(void) close(fd);
		(void) unlink(tmpl);
		return (errno);
	}

	while (fgets(line, sizeof (line), f) != NULL) {
		const char *p = strstr(line, needle);

		if (p == NULL)
			continue;
		(void) strlcpy(line_out, p, outlen);
		found = B_TRUE;
	}

	(void) fclose(f);
	(void) unlink(tmpl);

	return (found ? 0 : ENOENT);
}

/*
 * Print the engine's apply-era failure diagnostics, if any are in
 * the dbgmsg ring, so a failed rebase's FAIL line carries its
 * reason without a second run. The probes match only failure
 * lines ("apply copy" does not match the success tally's "apply
 * copies"), so tests that never reach a failing apply print
 * nothing. Each line is the LAST of its kind in the ring, so
 * output after an earlier test's failure can be stale -- read it
 * as "most recent occurrence", not necessarily "this test".
 */
void
rt_rebase_diag(void)
{
	static const char *const probes[] = {
		"rebase: apply own", "rebase: apply setup",
		"rebase: apply parent missing",
		"rebase: apply destination exists",
		"rebase: apply copy", "rebase: apply unlink",
		"rebase: rollback of"
	};
	char line[512];

	for (size_t i = 0; i < sizeof (probes) / sizeof (probes[0]);
	    i++) {
		if (rt_dbgmsg_last(probes[i], line,
		    sizeof (line)) == 0)
			(void) printf("\n    [diag] %s", line);
	}
}

/*
 * Scrape the engine's walk-summary counters from the dbgmsg ring:
 * the classification observable defined by the H matrix preamble.
 * Returns ENOENT if no run has logged a summary (the walk only
 * logs on success).
 */
int
rt_walk_stats(rt_walk_stats_t *ws)
{
	char line[512];
	unsigned long long v, hl, hr, lk;
	int err;

	err = rt_dbgmsg_last("rebase: walk visited", line,
	    sizeof (line));
	if (err != 0)
		return (err);

	if (sscanf(line, "rebase: walk visited %llu paths, "
	    "hysterical left %llu right %llu, "
	    "linkpool-member paths %llu", &v, &hl, &hr, &lk) != 4)
		return (ENOENT);

	ws->rws_visited = v;
	ws->rws_hyst_left = hl;
	ws->rws_hyst_right = hr;
	ws->rws_linked = lk;
	return (0);
}

/*
 * Scrape the changelist counts from the walk's second summary
 * line: the record-level observable defined by the D matrix
 * preamble.
 */
int
rt_changelist_counts(uint64_t *leftp, uint64_t *rightp)
{
	char line[512];
	unsigned long long l, r;
	int err;

	err = rt_dbgmsg_last("rebase: changelists left", line,
	    sizeof (line));
	if (err != 0)
		return (err);

	if (sscanf(line, "rebase: changelists left %llu right %llu",
	    &l, &r) != 2)
		return (ENOENT);

	*leftp = l;
	*rightp = r;
	return (0);
}

/*
 * Scrape the move-collapse counters from the walk's third summary
 * line: the collapse observable defined by the M matrix preamble.
 */
int
rt_move_stats(rt_move_stats_t *ms)
{
	char line[512];
	unsigned long long ml, mr, mel, mer;
	int err;

	err = rt_dbgmsg_last("rebase: moves left", line,
	    sizeof (line));
	if (err != 0)
		return (err);

	if (sscanf(line, "rebase: moves left %llu right %llu, "
	    "move-edits left %llu right %llu",
	    &ml, &mr, &mel, &mer) != 4)
		return (ENOENT);

	ms->rms_moves_left = ml;
	ms->rms_moves_right = mr;
	ms->rms_move_edits_left = mel;
	ms->rms_move_edits_right = mer;
	return (0);
}

/*
 * Scrape the linkpool classification tallies (phase A) and the
 * membership target tallies (phase B) from their per-branch
 * summary lines. Literal formats only: the lines are byte-stable
 * contracts and non-literal sscanf formats are a warning class we
 * do not want to negotiate with.
 */
int
rt_anchor_stats(rt_anchor_stats_t *as)
{
	char line[512];
	unsigned long long a, n, r, f;
	int err;

	err = rt_dbgmsg_last("rebase: linkpools left", line,
	    sizeof (line));
	if (err != 0)
		return (err);
	if (sscanf(line, "rebase: linkpools left anchored %llu "
	    "novel %llu recycled %llu fragment %llu",
	    &a, &n, &r, &f) != 4)
		return (ENOENT);
	as->ras_left[0] = a;
	as->ras_left[1] = n;
	as->ras_left[2] = r;
	as->ras_left[3] = f;

	err = rt_dbgmsg_last("rebase: linkpools right", line,
	    sizeof (line));
	if (err != 0)
		return (err);
	if (sscanf(line, "rebase: linkpools right anchored %llu "
	    "novel %llu recycled %llu fragment %llu",
	    &a, &n, &r, &f) != 4)
		return (ENOENT);
	as->ras_right[0] = a;
	as->ras_right[1] = n;
	as->ras_right[2] = r;
	as->ras_right[3] = f;
	return (0);
}

int
rt_target_stats(rt_target_stats_t *ts)
{
	char line[512];
	unsigned long long s, g, st, a, f, n;
	int err;

	err = rt_dbgmsg_last("rebase: targets left", line,
	    sizeof (line));
	if (err != 0)
		return (err);
	if (sscanf(line, "rebase: targets left same %llu gone %llu "
	    "standalone %llu anchor %llu fragment %llu novel %llu",
	    &s, &g, &st, &a, &f, &n) != 6)
		return (ENOENT);
	ts->rts_left[0] = s;
	ts->rts_left[1] = g;
	ts->rts_left[2] = st;
	ts->rts_left[3] = a;
	ts->rts_left[4] = f;
	ts->rts_left[5] = n;

	err = rt_dbgmsg_last("rebase: targets right", line,
	    sizeof (line));
	if (err != 0)
		return (err);
	if (sscanf(line, "rebase: targets right same %llu gone %llu "
	    "standalone %llu anchor %llu fragment %llu novel %llu",
	    &s, &g, &st, &a, &f, &n) != 6)
		return (ENOENT);
	ts->rts_right[0] = s;
	ts->rts_right[1] = g;
	ts->rts_right[2] = st;
	ts->rts_right[3] = a;
	ts->rts_right[4] = f;
	ts->rts_right[5] = n;
	return (0);
}

/*
 * Scrape the membership-merge finals and conflict tallies (phase
 * C/D observables; see the U/R matrix preamble).
 */
int
rt_final_stats(rt_final_stats_t *fs)
{
	char line[512];
	unsigned long long sm, g, st, a, f, n, c;
	int err;

	err = rt_dbgmsg_last("rebase: finals", line, sizeof (line));
	if (err != 0)
		return (err);
	if (sscanf(line, "rebase: finals same %llu gone %llu "
	    "standalone %llu anchor %llu fragment %llu novel %llu "
	    "conflict %llu", &sm, &g, &st, &a, &f, &n, &c) != 7)
		return (ENOENT);
	fs->rfs_kind[0] = sm;
	fs->rfs_kind[1] = g;
	fs->rfs_kind[2] = st;
	fs->rfs_kind[3] = a;
	fs->rfs_kind[4] = f;
	fs->rfs_kind[5] = n;
	fs->rfs_kind[6] = c;
	return (0);
}

int
rt_conflict_stats(rt_conflict_stats_t *cs)
{
	char line[512];
	unsigned long long t, r, d, o, c;
	int err;

	err = rt_dbgmsg_last("rebase: conflicts", line,
	    sizeof (line));
	if (err != 0)
		return (err);
	if (sscanf(line, "rebase: conflicts total %llu relink %llu "
	    "divergent %llu overlap %llu content %llu",
	    &t, &r, &d, &o, &c) != 5)
		return (ENOENT);
	cs->rcs_total = t;
	cs->rcs_relink = r;
	cs->rcs_divergent = d;
	cs->rcs_overlap = o;
	cs->rcs_content = c;
	return (0);
}

/*
 * Manifest accessors are defensive: a missing key returns
 * UINT64_MAX instead of aborting the harness, so a test asserting
 * against an engine that does not emit that key yet FAILs cleanly
 * rather than killing the whole run. (The v2 engine grows manifest
 * emission with the emit issues; until then crossref-era tests are
 * expected to fail, not crash.)
 */
uint64_t
rt_manifest_nconflicts(nvlist_t *nvl)
{
	uint64_t v;

	if (nvlist_lookup_uint64(nvl, "nconflicts", &v) != 0)
		return (UINT64_MAX);
	return (v);
}

uint64_t
rt_manifest_left_nchanges(nvlist_t *nvl)
{
	uint64_t v;

	if (nvlist_lookup_uint64(nvl, "left_nchanges", &v) != 0)
		return (UINT64_MAX);
	return (v);
}

uint64_t
rt_manifest_right_nchanges(nvlist_t *nvl)
{
	uint64_t v;

	if (nvlist_lookup_uint64(nvl, "right_nchanges", &v) != 0)
		return (UINT64_MAX);
	return (v);
}

/*
 * Match a manifest path (has leading "/") against a test path
 * (specified without the leading "/").
 */
static boolean_t
path_match(const char *manifest_path, const char *test_path)
{
	if (manifest_path[0] == '/' && test_path[0] != '/')
		return (strcmp(manifest_path + 1, test_path) == 0);
	return (strcmp(manifest_path, test_path) == 0);
}

boolean_t
rt_manifest_has_conflict(nvlist_t *nvl, const char *type, const char *path)
{
	nvlist_t **arr;
	uint_t n;
	int err;

	err = nvlist_lookup_nvlist_array(nvl, "conflicts", &arr, &n);
	if (err != 0)
		return (B_FALSE);

	for (uint_t i = 0; i < n; i++) {
		const char *ct = fnvlist_lookup_string(arr[i], "type");
		const char *cp = fnvlist_lookup_string(arr[i], "path");
		if (strcmp(ct, type) == 0 && path_match(cp, path))
			return (B_TRUE);
	}
	return (B_FALSE);
}

uint64_t
rt_manifest_nwarnings(nvlist_t *nvl)
{
	uint64_t v;

	if (nvlist_lookup_uint64(nvl, "nwarnings", &v) != 0)
		return (UINT64_MAX);
	return (v);
}

uint64_t
rt_manifest_nactions(nvlist_t *nvl)
{
	uint64_t v;

	if (nvlist_lookup_uint64(nvl, "nactions", &v) != 0)
		return (UINT64_MAX);
	return (v);
}

boolean_t
rt_manifest_has_warning(nvlist_t *nvl, const char *kind,
    const char *path)
{
	nvlist_t **arr;
	uint_t n;
	int err;

	err = nvlist_lookup_nvlist_array(nvl, "warnings", &arr, &n);
	if (err != 0)
		return (B_FALSE);

	for (uint_t i = 0; i < n; i++) {
		const char *wk = fnvlist_lookup_string(arr[i], "kind");
		const char *wp = fnvlist_lookup_string(arr[i], "path");
		if (strcmp(wk, kind) == 0 && path_match(wp, path))
			return (B_TRUE);
	}
	return (B_FALSE);
}

uint64_t
rt_manifest_conflict_nalt(nvlist_t *nvl, const char *path)
{
	nvlist_t **arr;
	uint_t n;
	int err;

	err = nvlist_lookup_nvlist_array(nvl, "conflicts", &arr, &n);
	if (err != 0)
		return (0);

	for (uint_t i = 0; i < n; i++) {
		const char *cp = fnvlist_lookup_string(arr[i], "path");
		if (path_match(cp, path)) {
			uint64_t nalt;

			if (nvlist_lookup_uint64(arr[i], "nalt",
			    &nalt) != 0)
				return (0);
			return (nalt);
		}
	}
	return (0);
}

void
rt_manifest_dump(nvlist_t *nvl)
{
	uint64_t nc, lc, rc;

	nc = rt_manifest_nconflicts(nvl);
	lc = rt_manifest_left_nchanges(nvl);
	rc = rt_manifest_right_nchanges(nvl);

	if (nc == UINT64_MAX && lc == UINT64_MAX && rc == UINT64_MAX) {
		(void) printf("\n    [manifest] absent (engine does "
		    "not emit yet)\n");
		return;
	}

	(void) printf("\n    [manifest] nconflicts=%llu "
	    "left_nchanges=%llu right_nchanges=%llu\n",
	    (unsigned long long)nc,
	    (unsigned long long)lc,
	    (unsigned long long)rc);

	if (nc > 0 && nc != UINT64_MAX) {
		nvlist_t **arr;
		uint_t n;
		if (nvlist_lookup_nvlist_array(nvl,
		    "conflicts", &arr, &n) == 0) {
			for (uint_t i = 0; i < n; i++) {
				const char *t =
				    fnvlist_lookup_string(arr[i], "type");
				const char *p =
				    fnvlist_lookup_string(arr[i], "path");
				(void) printf("    [%u] %s @ %s\n",
				    i, t, p);
			}
		}
	}
}
