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

uint64_t
rt_manifest_nconflicts(nvlist_t *nvl)
{
	return (fnvlist_lookup_uint64(nvl, "nconflicts"));
}

uint64_t
rt_manifest_left_nchanges(nvlist_t *nvl)
{
	return (fnvlist_lookup_uint64(nvl, "left_nchanges"));
}

uint64_t
rt_manifest_right_nchanges(nvlist_t *nvl)
{
	return (fnvlist_lookup_uint64(nvl, "right_nchanges"));
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
		if (path_match(cp, path))
			return (fnvlist_lookup_uint64(arr[i], "nalt"));
	}
	return (0);
}

void
rt_manifest_dump(nvlist_t *nvl)
{
	uint64_t nc, lc, rc;

	nc = fnvlist_lookup_uint64(nvl, "nconflicts");
	lc = fnvlist_lookup_uint64(nvl, "left_nchanges");
	rc = fnvlist_lookup_uint64(nvl, "right_nchanges");

	(void) printf("\n    [manifest] nconflicts=%llu "
	    "left_nchanges=%llu right_nchanges=%llu\n",
	    (unsigned long long)nc,
	    (unsigned long long)lc,
	    (unsigned long long)rc);

	if (nc > 0) {
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
