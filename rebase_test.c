// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Test harness for zfs rebase diff/collapse logic.
 *
 * Builds against libzpool to exercise the kernel rebase code in
 * userspace.  Creates pools on file vdevs, populates ZPL-compatible
 * datasets via raw DMU/ZAP/SA calls, takes snapshots, clones, and
 * calls dsl_rebase() to verify the diff and collapse phases complete
 * without errors.
 *
 * dsl_rebase() currently returns ENOSYS after a successful
 * diff+collapse (the apply phase is not yet implemented), so
 * ENOSYS is the "success" sentinel for these tests.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_rebase.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_znode.h>
#include <sys/dnode.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/sa_impl.h>
#include <sys/txg.h>

extern void kernel_init(int);
extern void kernel_fini(void);


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define	POOL_NAME	"rtest"
#define	VDEV_SIZE	(128ULL << 20)	/* 128 MiB */
#define	VDEV_PATH	"/tmp/rtest_vdev"

#define	ZFS_DIRENT_MAKE(type, obj)	((uint64_t)(obj) | \
	    ((uint64_t)(type) << 60))

static int tests_run;
static int tests_passed;
static int tests_failed;

#define	VERIFY_OK(err, msg) do {					\
	int _e = (err);						\
	if (_e != 0) {							\
		(void) fprintf(stderr, "FAIL: %s: %s (%d)\n",		\
		    (msg), strerror(_e), _e);				\
		return (_e);						\
	}								\
} while (0)

#define	TEST_START(name) do {						\
	(void) printf("  %-50s ", (name));				\
	(void) fflush(stdout);						\
	tests_run++;							\
} while (0)

#define	TEST_PASS() do {						\
	(void) printf("PASS\n");					\
	tests_passed++;							\
	return (0);							\
} while (0)

#define	TEST_FAIL(msg) do {						\
	(void) printf("FAIL: %s\n", (msg));				\
	tests_failed++;							\
	return (1);							\
} while (0)

#define	TEST_EXPECT(cond, msg) do {					\
	if (!(cond)) {							\
		TEST_FAIL(msg);						\
	}								\
} while (0)

/* ------------------------------------------------------------------ */
/*  Pool creation helpers                                              */
/* ------------------------------------------------------------------ */

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

	child = make_file_vdev(path, size);

	root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&child, 1);
	fnvlist_free(child);

	return (root);
}

static int
create_pool(const char *name, const char *vdev_path, size_t vdev_size)
{
	nvlist_t *nvroot;
	int err;

	nvroot = make_vdev_root(vdev_path, vdev_size);
	err = spa_create(name, nvroot, NULL, NULL, NULL);
	fnvlist_free(nvroot);

	return (err);
}

static void
destroy_pool(const char *name)
{
	(void) spa_destroy(name);
}

/* ------------------------------------------------------------------ */
/*  ZPL dataset creation via raw DMU/ZAP/SA                            */
/* ------------------------------------------------------------------ */

/*
 * Objset creation callback: sets up a minimal ZPL-compatible
 * structure (MASTER_NODE, SA registration, root directory).
 *
 * We bypass zfs_create_fs() entirely because it depends on
 * VFS structures (zfsvfs_t, znode_t, ACLs) that don't exist
 * in the libzpool userspace environment.  Instead we create
 * the same on-disk layout using only DMU/ZAP/SA primitives.
 */
static void
zpl_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	(void) arg;
	(void) cr;

	uint64_t version = ZPL_VERSION;
	uint64_t sa_obj, delq_obj, root_obj;
	sa_attr_type_t *sa_tbl;
	sa_handle_t *hdl;
	sa_bulk_attr_t attrs[16];
	int cnt = 0;

	uint64_t mode = S_IFDIR | 0755;
	uint64_t gen = dmu_tx_get_txg(tx);
	uint64_t size = 2;
	uint64_t links = 2;
	uint64_t uid = 0, gid = 0;
	uint64_t pflags = 0;
	uint64_t rdev = 0;
	uint64_t parent;
	uint64_t atime[2] = {0, 0};
	uint64_t mtime[2] = {0, 0};
	uint64_t ctime[2] = {0, 0};
	uint64_t crtime[2] = {0, 0};
	uint64_t dacl_count = 0;

	/* 1. Create MASTER_NODE at well-known obj 1. */
	VERIFY0(zap_create_claim(os, MASTER_NODE_OBJ,
	    DMU_OT_MASTER_NODE, DMU_OT_NONE, 0, tx));

	/* 2. Set ZPL version. */
	VERIFY0(zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
	    8, 1, &version, tx));

	/* 3. Create SA master node and register it. */
	sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
	    DMU_OT_NONE, 0, tx);
	VERIFY0(zap_add(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS,
	    8, 1, &sa_obj, tx));

	/* 4. Create delete queue. */
	delq_obj = zap_create(os, DMU_OT_UNLINKED_SET,
	    DMU_OT_NONE, 0, tx);
	VERIFY0(zap_add(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET,
	    8, 1, &delq_obj, tx));

	/* 5. Register SA attributes. */
	VERIFY0(sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &sa_tbl));

	/*
	 * 6. Create root directory object.
	 * Type is DMU_OT_DIRECTORY_CONTENTS (a ZAP), bonus type
	 * is DMU_OT_SA so we can store SA attributes in the bonus
	 * buffer.
	 */
	root_obj = zap_create_norm(os, 0,
	    DMU_OT_DIRECTORY_CONTENTS, DMU_OT_SA,
	    DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);

	parent = root_obj;

	/* 7. Set SA attributes on root dir. */
	VERIFY0(sa_handle_get(os, root_obj, NULL, SA_HDL_SHARED,
	    &hdl));

	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_MODE],
	    NULL, &mode, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_SIZE],
	    NULL, &size, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_GEN],
	    NULL, &gen, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_UID],
	    NULL, &uid, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_GID],
	    NULL, &gid, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_PARENT],
	    NULL, &parent, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_FLAGS],
	    NULL, &pflags, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_ATIME],
	    NULL, atime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_MTIME],
	    NULL, mtime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_CTIME],
	    NULL, ctime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_CRTIME],
	    NULL, crtime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_LINKS],
	    NULL, &links, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_DACL_COUNT],
	    NULL, &dacl_count, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_RDEV],
	    NULL, &rdev, 8);

	VERIFY0(sa_replace_all_by_template(hdl, attrs, cnt, tx));

	sa_handle_destroy(hdl);

	/* 8. Register root obj in MASTER_NODE. */
	VERIFY0(zap_add(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ,
	    8, 1, &root_obj, tx));
}

/* ------------------------------------------------------------------ */
/*  Object manipulation helpers                                        */
/* ------------------------------------------------------------------ */

static int ensure_sa_setup(objset_t *, sa_attr_type_t **);

/*
 * Look up a directory entry and return the decoded object number.
 * ZPL directory entries are encoded with ZFS_DIRENT_MAKE.
 */
static int
dir_lookup_obj(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t *objp)
{
	uint64_t raw;
	int err;

	err = zap_lookup(os, dir_obj, name, 8, 1, &raw);
	if (err != 0)
		return (err);
	*objp = ZFS_DIRENT_OBJ(raw);
	return (0);
}

/*
 * Open a dataset and set up SA.
 * Caller must release with dmu_objset_disown(os, B_FALSE, tag)
 * using the same tag passed here.
 */
static int
open_dataset(const char *dsname, objset_t **osp, const void *tag)
{
	sa_attr_type_t *tbl;
	int err;

	err = dmu_objset_own(dsname, DMU_OST_ZFS, B_FALSE, B_FALSE,
	    tag, osp);
	if (err != 0)
		return (err);

	err = ensure_sa_setup(*osp, &tbl);
	if (err != 0) {
		dmu_objset_disown(*osp, B_FALSE, tag);
		return (err);
	}

	return (0);
}

/*
 * Look up the root directory object number from MASTER_NODE.
 */
static int
get_root_obj(objset_t *os, uint64_t *root_objp)
{
	return (zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ,
	    8, 1, root_objp));
}

/*
 * Ensure SA is set up for an objset and return its attribute table.
 * Must be called after dmu_objset_hold() before any SA operations,
 * because os->os_sa is per-handle, not persisted on disk.
 */
static int
ensure_sa_setup(objset_t *os, sa_attr_type_t **tblp)
{
	uint64_t sa_obj = 0;
	int err;

	if (os->os_sa != NULL) {
		*tblp = os->os_sa->sa_user_table;
		return (0);
	}

	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS,
	    8, 1, &sa_obj);
	if (err != 0 && err != ENOENT)
		return (err);

	return (sa_setup(os, sa_obj, zfs_attr_table, ZPL_END, tblp));
}

static sa_attr_type_t *
get_sa_table(objset_t *os)
{
	return (os->os_sa->sa_user_table);
}

/*
 * Set SA attributes on a newly allocated object.
 * For files: mode = S_IFREG|0644, size = data_len.
 * For dirs:  mode = S_IFDIR|0755, size = 2.
 */
static int
set_sa_attrs(objset_t *os, uint64_t obj, uint64_t parent_obj,
    uint64_t mode_val, uint64_t size_val, dmu_tx_t *tx)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	sa_bulk_attr_t attrs[16];
	int cnt = 0;
	int err;

	uint64_t gen = dmu_tx_get_txg(tx);
	uint64_t links = 1;
	uint64_t uid = 0, gid = 0;
	uint64_t pflags = 0;
	uint64_t rdev = 0;
	uint64_t atime[2] = {0, 0};
	uint64_t mtime[2] = {0, 0};
	uint64_t ctime[2] = {0, 0};
	uint64_t crtime[2] = {0, 0};
	uint64_t dacl_count = 0;

	err = sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_MODE],
	    NULL, &mode_val, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_SIZE],
	    NULL, &size_val, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_GEN],
	    NULL, &gen, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_UID],
	    NULL, &uid, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_GID],
	    NULL, &gid, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_PARENT],
	    NULL, &parent_obj, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_FLAGS],
	    NULL, &pflags, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_ATIME],
	    NULL, atime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_MTIME],
	    NULL, mtime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_CTIME],
	    NULL, ctime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_CRTIME],
	    NULL, crtime, 16);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_LINKS],
	    NULL, &links, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_DACL_COUNT],
	    NULL, &dacl_count, 8);
	SA_ADD_BULK_ATTR(attrs, cnt, sa_tbl[ZPL_RDEV],
	    NULL, &rdev, 8);

	err = sa_replace_all_by_template(hdl, attrs, cnt, tx);

	sa_handle_destroy(hdl);

	return (err);
}

/*
 * Create a regular file in a directory.
 * Allocates a DMU object, writes `data` into it, sets SA attrs,
 * and adds a ZAP entry in the parent directory.
 *
 * Returns the new file's object number via *objp.
 */
static int
test_create_file(objset_t *os, uint64_t dir_obj, const char *name,
    const void *data, uint64_t datalen, uint64_t *objp)
{
	dmu_tx_t *tx;
	uint64_t obj;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);
	dmu_tx_hold_sa_create(tx, DN_BONUS_SIZE(DNODE_MIN_SIZE));
	if (datalen > 0)
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, datalen);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	obj = dmu_object_alloc(os, DMU_OT_PLAIN_FILE_CONTENTS, 0,
	    DMU_OT_SA, DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);

	err = set_sa_attrs(os, obj, dir_obj,
	    S_IFREG | 0644, datalen, tx);
	if (err != 0) {
		dmu_tx_commit(tx);
		return (err);
	}

	if (datalen > 0)
		dmu_write(os, obj, 0, datalen, data, tx, 0);

	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG), obj);
		err = zap_add(os, dir_obj, name, 8, 1, &dirent, tx);
	}
	dmu_tx_commit(tx);

	if (objp != NULL)
		*objp = obj;

	return (err);
}

/*
 * Create a subdirectory in a directory.
 * Allocates a ZAP object for the new dir's contents, sets SA
 * attrs, and adds a ZAP entry in the parent.
 */
static int
test_create_dir(objset_t *os, uint64_t parent_obj, const char *name,
    uint64_t *objp)
{
	dmu_tx_t *tx;
	uint64_t obj;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, parent_obj, B_TRUE, name);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	dmu_tx_hold_sa_create(tx, DN_BONUS_SIZE(DNODE_MIN_SIZE));

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	obj = zap_create_norm(os, 0, DMU_OT_DIRECTORY_CONTENTS,
	    DMU_OT_SA, DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);

	err = set_sa_attrs(os, obj, parent_obj,
	    S_IFDIR | 0755, 2, tx);
	if (err != 0) {
		dmu_tx_commit(tx);
		return (err);
	}

	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFDIR), obj);
		err = zap_add(os, parent_obj, name, 8, 1, &dirent, tx);
	}
	dmu_tx_commit(tx);

	if (objp != NULL)
		*objp = obj;

	return (err);
}

/*
 * Remove a directory entry.
 */
static int
test_remove_entry(objset_t *os, uint64_t dir_obj, const char *name)
{
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_FALSE, name);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	err = zap_remove(os, dir_obj, name, tx);
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Overwrite file data (edit in place, same dnode).
 */
static int
test_edit_file(objset_t *os, uint64_t obj, const void *data,
    uint64_t datalen)
{
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, obj, 0, datalen);
	dmu_tx_hold_bonus(tx, obj);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	dmu_write(os, obj, 0, datalen, data, tx, 0);

	/* Update ZPL_SIZE via SA. */
	{
		sa_attr_type_t *sa_tbl = get_sa_table(os);
		sa_handle_t *hdl;

		VERIFY0(sa_handle_get(os, obj, NULL,
		    SA_HDL_SHARED, &hdl));
		VERIFY0(sa_update(hdl, sa_tbl[ZPL_SIZE],
		    &datalen, 8, tx));
		sa_handle_destroy(hdl);
	}

	dmu_tx_commit(tx);

	return (0);
}

/*
 * Add a hardlink: add a ZAP entry pointing to an existing object.
 */
static int
test_add_hardlink(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t target_obj)
{
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG), target_obj);
		err = zap_add(os, dir_obj, name, 8, 1, &dirent, tx);
	}
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Simulate a rename-on-save (nvim-style hysterical edit):
 * allocate a NEW dnode, copy data from old dnode, remove old
 * ZAP entry, add new ZAP entry with same name.
 */
static int
test_hysterical_edit(objset_t *os, uint64_t dir_obj,
    const char *name, const void *data, uint64_t datalen,
    uint64_t *new_objp)
{
	dmu_tx_t *tx;
	uint64_t old_obj, new_obj;
	int err;

	err = dir_lookup_obj(os, dir_obj, name, &old_obj);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);
	dmu_tx_hold_zap(tx, dir_obj, B_FALSE, name);
	dmu_tx_hold_sa_create(tx, DN_BONUS_SIZE(DNODE_MIN_SIZE));
	if (datalen > 0)
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, datalen);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	new_obj = dmu_object_alloc(os, DMU_OT_PLAIN_FILE_CONTENTS, 0,
	    DMU_OT_SA, DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);

	err = set_sa_attrs(os, new_obj, dir_obj,
	    S_IFREG | 0644, datalen, tx);
	if (err != 0) {
		dmu_tx_commit(tx);
		return (err);
	}

	if (datalen > 0)
		dmu_write(os, new_obj, 0, datalen, data, tx, 0);

	VERIFY0(zap_remove(os, dir_obj, name, tx));
	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG), new_obj);
		VERIFY0(zap_add(os, dir_obj, name, 8, 1, &dirent, tx));
	}

	dmu_tx_commit(tx);

	if (new_objp != NULL)
		*new_objp = new_obj;

	return (0);
}

/* ------------------------------------------------------------------ */
/*  Dataset / snapshot / clone helpers                                  */
/* ------------------------------------------------------------------ */

/*
 * Force all pending transactions to disk.
 * Must be called before snapshotting or running rebase so that
 * the on-disk state is consistent.
 */
static void
sync_pool(void)
{
	spa_t *spa;

	VERIFY0(spa_open(POOL_NAME, &spa, FTAG));
	txg_wait_synced(spa_get_dsl(spa), 0);
	spa_close(spa, FTAG);
}

static int
create_zpl_dataset(const char *dsname)
{
	return (dmu_objset_create(dsname, DMU_OST_ZFS, 0, NULL,
	    zpl_create_cb, NULL));
}

static int
take_snapshot(const char *dsname, const char *snapname)
{
	return (dmu_objset_snapshot_one(dsname, snapname));
}

static int
clone_dataset(const char *clone_name, const char *snap_name)
{
	return (dsl_dataset_clone(clone_name, snap_name));
}

/* ------------------------------------------------------------------ */
/*  Test scaffolding: set up src dataset, snapshot, clone to left+right */
/* ------------------------------------------------------------------ */

/*
 * Create a pool and a base dataset with one file ("hello")
 * and one subdirectory ("subdir") containing one file ("inner").
 */
static int
scaffold_basic(void)
{
	objset_t *os;
	uint64_t root, subdir_obj;
	int err;

	(void) spa_destroy(POOL_NAME);
	(void) unlink(VDEV_PATH);

	err = create_pool(POOL_NAME, VDEV_PATH, VDEV_SIZE);
	VERIFY_OK(err, "create_pool");

	err = create_zpl_dataset(POOL_NAME "/src");
	VERIFY_OK(err, "create src dataset");

	err = open_dataset(POOL_NAME "/src", &os, FTAG);
	VERIFY_OK(err, "hold src dataset");

	err = get_root_obj(os, &root);
	VERIFY_OK(err, "get root obj");

	err = test_create_file(os, root, "hello",
	    "world\n", 6, NULL);
	VERIFY_OK(err, "create file 'hello'");

	err = test_create_dir(os, root, "subdir", &subdir_obj);
	VERIFY_OK(err, "create dir 'subdir'");

	err = test_create_file(os, subdir_obj, "inner",
	    "nested\n", 7, NULL);
	VERIFY_OK(err, "create file 'subdir/inner'");

	dmu_objset_disown(os, B_FALSE, FTAG);

	sync_pool();

	err = take_snapshot(POOL_NAME "/src", "base");
	VERIFY_OK(err, "snapshot src@base");

	err = clone_dataset(POOL_NAME "/left",
	    POOL_NAME "/src@base");
	VERIFY_OK(err, "clone left");

	err = clone_dataset(POOL_NAME "/right",
	    POOL_NAME "/src@base");
	VERIFY_OK(err, "clone right");

	return (0);
}

static void
scaffold_teardown(void)
{
	destroy_pool(POOL_NAME);
	(void) unlink(VDEV_PATH);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * T1: Smoke test — no changes on either side.
 * Both left and right are identical to base.
 * Diff should produce empty changelists, collapse should be a no-op.
 */
static int
test_smoke_no_changes(void)
{
	int err;

	TEST_START("smoke: no changes on either side");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);

	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS,
	    "expected ENOSYS from successful diff+collapse");

	TEST_PASS();
}

/*
 * T2: Left adds a file, right unchanged.
 */
static int
test_left_add(void)
{
	objset_t *os;
	uint64_t root;
	int err;

	TEST_START("left adds a file, right unchanged");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	err = test_create_file(os, root, "newfile",
	    "added\n", 6, NULL);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("create newfile");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T3: Right adds a file, left unchanged.
 */
static int
test_right_add(void)
{
	objset_t *os;
	uint64_t root;
	int err;

	TEST_START("right adds a file, left unchanged");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/right", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold right");
	}

	VERIFY0(get_root_obj(os, &root));
	err = test_create_file(os, root, "rightfile",
	    "from-right\n", 11, NULL);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("create rightfile");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T4: Left deletes a file.
 */
static int
test_left_delete(void)
{
	objset_t *os;
	uint64_t root;
	int err;

	TEST_START("left deletes a file");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	err = test_remove_entry(os, root, "hello");
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("remove hello");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T5: Left edits a file (same dnode, new content).
 */
static int
test_left_edit(void)
{
	objset_t *os;
	uint64_t root, obj;
	int err;

	TEST_START("left edits a file in-place");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	err = dir_lookup_obj(os, root, "hello", &obj);
	if (err != 0) {
		dmu_objset_disown(os, B_FALSE, FTAG);
		scaffold_teardown();
		TEST_FAIL("lookup hello");
	}

	err = test_edit_file(os, obj, "edited\n", 7);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("edit hello");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T6: Hysterical edit on left (rename-on-save, same content).
 * New dnode with same data — should be detected as non-edit.
 */
static int
test_hysterical_file(void)
{
	objset_t *os;
	uint64_t root;
	int err;

	TEST_START("hysterical edit (nvim-style, same content)");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));

	/* Replace "hello" with new dnode, same data. */
	err = test_hysterical_edit(os, root, "hello",
	    "world\n", 6, NULL);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hysterical edit");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T7: Move on left (rename: remove "hello", add "hello2"
 * pointing to same dnode).
 */
static int
test_left_move(void)
{
	objset_t *os;
	uint64_t root, obj;
	dmu_tx_t *tx;
	int err;

	TEST_START("left moves (renames) a file");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	err = dir_lookup_obj(os, root, "hello", &obj);
	if (err != 0) {
		dmu_objset_disown(os, B_FALSE, FTAG);
		scaffold_teardown();
		TEST_FAIL("lookup hello");
	}

	/* Rename: remove old entry, add new entry same obj. */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, root, B_FALSE, "hello");
	dmu_tx_hold_zap(tx, root, B_TRUE, "hello_renamed");
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		dmu_objset_disown(os, B_FALSE, FTAG);
		scaffold_teardown();
		TEST_FAIL("tx assign");
	}

	VERIFY0(zap_remove(os, root, "hello", tx));
	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG), obj);
		VERIFY0(zap_add(os, root, "hello_renamed", 8, 1,
		    &dirent, tx));
	}
	dmu_tx_commit(tx);
	dmu_objset_disown(os, B_FALSE, FTAG);

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T8: Hardlink on left (add new dir entry pointing to
 * existing dnode).
 */
static int
test_left_hardlink(void)
{
	objset_t *os;
	uint64_t root, obj;
	int err;

	TEST_START("left adds a hardlink");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	err = dir_lookup_obj(os, root, "hello", &obj);
	if (err != 0) {
		dmu_objset_disown(os, B_FALSE, FTAG);
		scaffold_teardown();
		TEST_FAIL("lookup hello");
	}

	err = test_add_hardlink(os, root, "hello_link", obj);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("add hardlink");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T9: Both sides add different files (no conflict — different
 * names).
 */
static int
test_both_add_different(void)
{
	objset_t *os;
	uint64_t root;
	int err;

	TEST_START("both sides add different files");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	/* Left adds "leftfile". */
	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}
	VERIFY0(get_root_obj(os, &root));
	err = test_create_file(os, root, "leftfile",
	    "left\n", 5, NULL);
	dmu_objset_disown(os, B_FALSE, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("create leftfile");
	}

	/* Right adds "rightfile". */
	err = open_dataset(POOL_NAME "/right", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold right");
	}
	VERIFY0(get_root_obj(os, &root));
	err = test_create_file(os, root, "rightfile",
	    "right\n", 6, NULL);
	dmu_objset_disown(os, B_FALSE, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("create rightfile");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T10: Left edits, right edits (same file, different content).
 * Both sides modify "hello" — this should succeed through
 * diff+collapse (conflict detection is cross-reference, issue 9).
 */
static int
test_both_edit(void)
{
	objset_t *os;
	uint64_t root, obj;
	int err;

	TEST_START("both sides edit same file");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	/* Left edits hello. */
	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}
	VERIFY0(get_root_obj(os, &root));
	VERIFY0(dir_lookup_obj(os, root, "hello", &obj));
	err = test_edit_file(os, obj, "left-edit\n", 10);
	dmu_objset_disown(os, B_FALSE, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("edit left");
	}

	/* Right edits hello. */
	err = open_dataset(POOL_NAME "/right", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold right");
	}
	VERIFY0(get_root_obj(os, &root));
	VERIFY0(dir_lookup_obj(os, root, "hello", &obj));
	err = test_edit_file(os, obj, "right-edit\n", 11);
	dmu_objset_disown(os, B_FALSE, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("edit right");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T11: Nested directory changes — edit a file deep in the tree.
 */
static int
test_nested_edit(void)
{
	objset_t *os;
	uint64_t root, subdir, obj;
	int err;

	TEST_START("nested: edit file inside subdirectory");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	VERIFY0(dir_lookup_obj(os, root, "subdir", &subdir));
	VERIFY0(dir_lookup_obj(os, subdir, "inner", &obj));

	err = test_edit_file(os, obj, "modified-inner\n", 15);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("edit inner");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T12: Multiple operations on left — add, delete, and edit in
 * the same changelist.
 */
static int
test_mixed_operations(void)
{
	objset_t *os;
	uint64_t root, obj;
	int err;

	TEST_START("mixed: add + delete + edit on left");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));

	/* Add a new file. */
	err = test_create_file(os, root, "brand_new",
	    "new content\n", 12, NULL);
	if (err != 0) {
		dmu_objset_disown(os, B_FALSE, FTAG);
		scaffold_teardown();
		TEST_FAIL("create brand_new");
	}

	/* Delete an existing file. */
	err = test_remove_entry(os, root, "hello");
	if (err != 0) {
		dmu_objset_disown(os, B_FALSE, FTAG);
		scaffold_teardown();
		TEST_FAIL("remove hello");
	}

	/* Edit an existing file in subdir. */
	uint64_t subdir;
	VERIFY0(dir_lookup_obj(os, root, "subdir", &subdir));
	VERIFY0(dir_lookup_obj(os, subdir, "inner", &obj));
	err = test_edit_file(os, obj, "modified\n", 9);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("edit inner");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/*
 * T13: Move + edit (file moved and its content changed).
 */
static int
test_move_edit(void)
{
	objset_t *os;
	uint64_t root, obj;
	dmu_tx_t *tx;
	int err;

	TEST_START("move + edit on left");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = open_dataset(POOL_NAME "/left", &os, FTAG);
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("hold left");
	}

	VERIFY0(get_root_obj(os, &root));
	VERIFY0(dir_lookup_obj(os, root, "hello", &obj));

	/* Rename: hello -> hello_moved. */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, root, B_FALSE, "hello");
	dmu_tx_hold_zap(tx, root, B_TRUE, "hello_moved");
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	VERIFY0(zap_remove(os, root, "hello", tx));
	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG), obj);
		VERIFY0(zap_add(os, root, "hello_moved", 8, 1,
		    &dirent, tx));
	}
	dmu_tx_commit(tx);

	/* Edit the file content. */
	err = test_edit_file(os, obj, "moved and edited\n", 17);
	dmu_objset_disown(os, B_FALSE, FTAG);

	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("edit moved file");
	}

	sync_pool();
	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == ENOSYS, "expected ENOSYS");
	TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Error case tests                                                    */
/* ------------------------------------------------------------------ */

/*
 * T14: left == right (same dataset) should fail.
 */
static int
test_error_same_dataset(void)
{
	int err;

	TEST_START("error: left == right (same dataset)");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	err = dsl_rebase(POOL_NAME "/left",
	    POOL_NAME "/left", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == EINVAL, "expected EINVAL");
	TEST_PASS();
}

/*
 * T15: left is a snapshot (invalid).
 */
static int
test_error_left_is_snapshot(void)
{
	int err;

	TEST_START("error: left is a snapshot");

	err = scaffold_basic();
	if (err != 0)
		TEST_FAIL("scaffold failed");

	/* Take a snapshot on left. */
	err = take_snapshot(POOL_NAME "/left", "snap1");
	if (err != 0) {
		scaffold_teardown();
		TEST_FAIL("snapshot left");
	}

	err = dsl_rebase(POOL_NAME "/left@snap1",
	    POOL_NAME "/right", NULL);
	scaffold_teardown();

	TEST_EXPECT(err == EINVAL, "expected EINVAL");
	TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

static void
run_tests(void)
{
	(void) printf("zfs rebase test suite\n");
	(void) printf("=====================\n\n");

	(void) test_smoke_no_changes();
	(void) test_left_add();
	(void) test_right_add();
	(void) test_left_delete();
	(void) test_left_edit();
	(void) test_hysterical_file();
	(void) test_left_move();
	(void) test_left_hardlink();
	(void) test_both_add_different();
	(void) test_both_edit();
	(void) test_nested_edit();
	(void) test_mixed_operations();
	(void) test_move_edit();
	(void) test_error_same_dataset();
	(void) test_error_left_is_snapshot();

	(void) printf("\n=====================\n");
	(void) printf("Results: %d/%d passed",
	    tests_passed, tests_run);
	if (tests_failed > 0)
		(void) printf(", %d FAILED", tests_failed);
	(void) printf("\n");
}

int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	run_tests();

	kernel_fini();

	return (tests_failed > 0 ? 1 : 0);
}
