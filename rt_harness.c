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
	if (err == ENOSYS) {
		*nvlp = nvl;
		return (0);
	}
	fnvlist_free(nvl);
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
