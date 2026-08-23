// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Pool and dataset scaffolding: file-vdev pool lifecycle, ZPL
 * dataset creation, snapshots, clones, and the standard three-way
 * scaffold (src populated, src@base snapshotted, left and right
 * cloned from it).
 */

#include "rebase_test.h"

static nvlist_t *
make_file_vdev(const char *path, size_t size)
{
	nvlist_t *file;
	int fd;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd == -1) {
		perror("open vdev file");
		abort();
	}
	if (ftruncate(fd, size) != 0) {
		perror("ftruncate vdev file");
		abort();
	}
	(void) close(fd);

	file = fnvlist_alloc();
	fnvlist_add_string(file, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(file, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(file, ZPOOL_CONFIG_ASHIFT, 12);

	return (file);
}

static nvlist_t *
make_vdev_root(const char *path, size_t size)
{
	nvlist_t *root, *child;
	const nvlist_t *children[1];

	child = make_file_vdev(path, size);
	children[0] = child;

	root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    children, 1);
	fnvlist_free(child);

	return (root);
}

static int
create_pool(void)
{
	nvlist_t *nvroot;
	int err;

	nvroot = make_vdev_root(VDEV_PATH, VDEV_SIZE);
	err = spa_create(POOL_NAME, nvroot, NULL, NULL, NULL);
	fnvlist_free(nvroot);

	return (err);
}

int
rt_create_zpl_dataset(const char *dsname)
{
	return (dmu_objset_create(dsname, DMU_OST_ZFS, 0, NULL,
	    rt_zpl_create_cb, NULL));
}

int
rt_snapshot(const char *dsname, const char *snapname)
{
	return (dmu_objset_snapshot_one(dsname, snapname));
}

int
rt_clone(const char *clone_name, const char *snap_name)
{
	return (dsl_dataset_clone(clone_name, snap_name));
}

/*
 * Create a zvol-typed dataset with no contents (no creation
 * callback): setup cell S14 only needs the objset TYPE, and the
 * engine's all-ZPL precondition fires before anything would read
 * the (absent) master node.
 */
int
rt_create_zvol_dataset(const char *dsname)
{
	return (dmu_objset_create(dsname, DMU_OST_ZVOL, 0, NULL,
	    NULL, NULL));
}

/*
 * Snapshot src as @base and clone it to left and right. Syncs the
 * pool first so the snapshot captures everything.
 */
int
rt_scaffold_snap_and_clone(void)
{
	int err;

	rt_sync_pool();

	err = rt_snapshot(RT_DS_SRC, "base");
	VERIFY_OK(err, "snapshot src@base");

	err = rt_clone(RT_DS_LEFT, RT_DS_SRC "@base");
	VERIFY_OK(err, "clone left");

	err = rt_clone(RT_DS_RIGHT, RT_DS_SRC "@base");
	VERIFY_OK(err, "clone right");

	return (0);
}

/*
 * Create a pool and an empty src dataset (no files). Caller
 * populates src, then calls rt_scaffold_snap_and_clone().
 */
int
rt_scaffold_empty_base(void)
{
	int err;

	(void) spa_destroy(POOL_NAME);
	(void) unlink(VDEV_PATH);

	err = create_pool();
	VERIFY_OK(err, "create_pool");

	err = rt_create_zpl_dataset(RT_DS_SRC);
	VERIFY_OK(err, "create src dataset");

	return (0);
}

/*
 * The standard scaffold: a base dataset with one file ("hello"),
 * one subdirectory ("subdir") containing one file ("inner"),
 * snapshotted and cloned to left + right.
 */
int
rt_scaffold_basic(void)
{
	rt_ds_t d;
	uint64_t subdir_obj;
	int err;

	err = rt_scaffold_empty_base();
	if (err != 0)
		return (err);

	err = rt_open(RT_DS_SRC, &d);
	VERIFY_OK(err, "hold src dataset");

	err = rt_create_file(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	if (err == 0)
		err = rt_create_dir(d.rtd_os, d.rtd_root, "subdir",
		    &subdir_obj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, subdir_obj, "inner",
		    "nested\n", 7, NULL);
	rt_close(&d);
	VERIFY_OK(err, "populate src");

	return (rt_scaffold_snap_and_clone());
}

void
rt_scaffold_teardown(void)
{
	(void) spa_destroy(POOL_NAME);
	(void) unlink(VDEV_PATH);
}
