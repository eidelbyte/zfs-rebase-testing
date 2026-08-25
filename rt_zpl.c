// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * ZPL-compatible object manipulation via raw DMU/ZAP/SA calls.
 *
 * We bypass zfs_create_fs() and the VFS layer entirely because they
 * depend on structures (zfsvfs_t, znode_t, ACLs) that don't exist in
 * the libzpool userspace environment. Instead we create the same
 * on-disk layout using only DMU/ZAP/SA primitives. Directory entries
 * are stored with ZFS_DIRENT_MAKE(type, obj) encoding to match real
 * ZPL layout.
 */

#include "rebase_test.h"

/*
 * Objset creation callback: sets up a minimal ZPL-compatible
 * structure (MASTER_NODE, SA registration, delete queue, root
 * directory).
 */
void
rt_zpl_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
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

/*
 * Ensure SA is set up for an objset and return its attribute table.
 * Must run after the objset is held/owned and before any SA
 * operation, because os->os_sa is per-handle, not persisted on disk.
 */
int
rt_sa_setup(objset_t *os, sa_attr_type_t **tblp)
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
 * Look up a directory entry and return the decoded object number.
 */
int
rt_dir_lookup(objset_t *os, uint64_t dir_obj, const char *name,
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
 * Create a regular file in a directory: allocate a DMU object,
 * write `data` into it, set SA attrs, add a ZAP entry in the parent.
 * Returns the new file's object number via *objp.
 */
int
rt_create_file(objset_t *os, uint64_t dir_obj, const char *name,
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
 * Create a subdirectory in a directory: allocate a ZAP object for
 * the new dir's contents, set SA attrs, add a ZAP entry in the
 * parent.
 */
int
rt_create_dir(objset_t *os, uint64_t parent_obj, const char *name,
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
 * Adjust ZPL_LINKS on an object by delta within an open tx.
 */
static void
adjust_nlink(objset_t *os, uint64_t obj, int64_t delta, dmu_tx_t *tx)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	uint64_t links;

	VERIFY0(sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl));
	VERIFY0(sa_lookup(hdl, sa_tbl[ZPL_LINKS], &links, 8));
	links += delta;
	VERIFY0(sa_update(hdl, sa_tbl[ZPL_LINKS], &links, 8, tx));
	sa_handle_destroy(hdl);
}

/*
 * Remove a directory entry with real ZPL semantics: unlinking a
 * plain file decrements its ZPL_LINKS (possibly to 0 -- the object
 * then lingers pathless, like a delete-queue orphan; the harness
 * never frees dnodes). Directory entries are removed without a
 * decrement: the harness does not maintain directory link counts,
 * and the rebase engine never reads them.
 */
int
rt_remove_entry(objset_t *os, uint64_t dir_obj, const char *name)
{
	uint64_t obj;
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	boolean_t isfile;
	int err;

	err = rt_dir_lookup(os, dir_obj, name, &obj);
	if (err != 0)
		return (err);
	err = dmu_object_info(os, obj, &doi);
	if (err != 0)
		return (err);
	isfile = (doi.doi_type == DMU_OT_PLAIN_FILE_CONTENTS);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_FALSE, name);
	if (isfile)
		dmu_tx_hold_bonus(tx, obj);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	err = zap_remove(os, dir_obj, name, tx);
	if (err == 0 && isfile)
		adjust_nlink(os, obj, -1, tx);
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Overwrite file data (edit in place, same dnode). The old tail
 * is freed like a real editor save would (open with O_TRUNC runs
 * zfs_trunc): without the free a shrinking edit leaves stale
 * blocks past EOF that no real ZPL dataset would carry, and the
 * apply copier honestly reproduces whatever is allocated.
 */
int
rt_edit_file(objset_t *os, uint64_t obj, const void *data,
    uint64_t datalen)
{
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, obj, 0, datalen);
	dmu_tx_hold_free(tx, obj, datalen, DMU_OBJECT_END);
	dmu_tx_hold_bonus(tx, obj);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	err = dmu_free_range(os, obj, datalen, DMU_OBJECT_END, tx);
	if (err != 0) {
		dmu_tx_commit(tx);
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
 * Add a hardlink with real ZPL semantics: a new ZAP entry pointing
 * at an existing object, and the object's ZPL_LINKS bumped. The
 * sprint-2 walker builds linkpool tables from ZPL_LINKS, so link
 * fixtures are invisible to it without the bump.
 */
int
rt_add_hardlink(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t target_obj)
{
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);
	dmu_tx_hold_bonus(tx, target_obj);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG),
		    target_obj);
		err = zap_add(os, dir_obj, name, 8, 1, &dirent, tx);
	}
	if (err == 0)
		adjust_nlink(os, target_obj, 1, tx);
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Corruption injector: set ZPL_LINKS to an arbitrary value without
 * touching directory entries. Used by the linkpool completeness
 * (LV) matrix cells to force rlp_nfound != rlp_nlink; the engine
 * treats the mismatch as corrupt input and returns EIO on every
 * build, debug included.
 */
int
rt_set_nlink(objset_t *os, uint64_t obj, uint64_t nlink)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, obj);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	VERIFY0(sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl));
	VERIFY0(sa_update(hdl, sa_tbl[ZPL_LINKS], &nlink, 8, tx));
	sa_handle_destroy(hdl);
	dmu_tx_commit(tx);

	return (0);
}

/*
 * Set a ZPL property key in the MASTER_NODE ZAP (e.g.
 * "casesensitivity", "normalization"), the same store
 * zfs_get_zplprop() reads. Used by the setup (S) matrix cells.
 */
int
rt_set_zplprop(objset_t *os, const char *name, uint64_t value)
{
	dmu_tx_t *tx;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE, name);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	err = zap_update(os, MASTER_NODE_OBJ, name, 8, 1, &value, tx);
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Corruption injector: a directory entry pointing at an object
 * number that was never allocated. The walk detects the dangling
 * reference and aborts the rebase cleanly with EIO (the engine
 * converts the underlying ENOENT at the visit boundary; a leaked
 * ENOENT would read as "no common ancestor").
 */
int
rt_add_dangling_entry(objset_t *os, uint64_t dir_obj, const char *name)
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
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG),
		    999999ULL);
		err = zap_add(os, dir_obj, name, 8, 1, &dirent, tx);
	}
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Simulate a rename-on-save (nvim-style hysterical edit): allocate
 * a NEW dnode, write the data, remove the old ZAP entry, add a new
 * ZAP entry with the same name.
 */
int
rt_hysterical_edit(objset_t *os, uint64_t dir_obj,
    const char *name, const void *data, uint64_t datalen,
    uint64_t *new_objp)
{
	dmu_tx_t *tx;
	uint64_t old_obj, new_obj;
	int err;

	err = rt_dir_lookup(os, dir_obj, name, &old_obj);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);
	dmu_tx_hold_zap(tx, dir_obj, B_FALSE, name);
	dmu_tx_hold_bonus(tx, old_obj);
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
	/* The replaced dnode loses this link, like a real unlink. */
	adjust_nlink(os, old_obj, -1, tx);
	{
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFREG),
		    new_obj);
		VERIFY0(zap_add(os, dir_obj, name, 8, 1, &dirent, tx));
	}

	dmu_tx_commit(tx);

	if (new_objp != NULL)
		*new_objp = new_obj;

	return (0);
}

/*
 * Set (create or update) one fixed-size uint64 SA attribute. The
 * hysteria (H) matrix cells use this to flip individual identity
 * attributes: mode, uid, gid, flags, projid, rdev -- and ZPL_GEN,
 * which simulates a recycled object number (same obj, new birth
 * certificate) without having to force real allocator reuse.
 */
int
rt_set_sa_u64(objset_t *os, uint64_t obj, int zpl_attr, uint64_t value)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	dmu_tx_t *tx;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_sa(tx, hdl, B_TRUE);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		sa_handle_destroy(hdl);
		return (err);
	}

	err = sa_update(hdl, sa_tbl[zpl_attr], &value, 8, tx);
	dmu_tx_commit(tx);
	sa_handle_destroy(hdl);

	return (err);
}

/*
 * Set (create or update) one variable-length SA attribute from a
 * byte buffer. Used for DACL_ACES (any differing bytes do -- the
 * engine compares by memcmp, validity is irrelevant), ZPL_SYMLINK
 * targets, and packed DXATTR nvlists.
 */
int
rt_set_sa_blob(objset_t *os, uint64_t obj, int zpl_attr,
    const void *buf, uint32_t len)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	dmu_tx_t *tx;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_sa(tx, hdl, B_TRUE);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		sa_handle_destroy(hdl);
		return (err);
	}

	err = sa_update(hdl, sa_tbl[zpl_attr], (void *)(uintptr_t)buf,
	    len, tx);
	dmu_tx_commit(tx);
	sa_handle_destroy(hdl);

	return (err);
}

/*
 * Corruption injector: remove an SA attribute outright. H33 strips
 * ZPL_GEN to assert the engine's hard-EIO stance on attributes a
 * ZPL >= 5 dataset guarantees; H24 strips ZPL_DXATTR to flip an
 * xattr set from SA-resident to directory form.
 */
int
rt_remove_sa_attr(objset_t *os, uint64_t obj, int zpl_attr)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	dmu_tx_t *tx;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_sa(tx, hdl, B_TRUE);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		sa_handle_destroy(hdl);
		return (err);
	}

	err = sa_remove(hdl, sa_tbl[zpl_attr], tx);
	dmu_tx_commit(tx);
	sa_handle_destroy(hdl);

	return (err);
}

/*
 * Simulate touch(1): bump ZPL_MTIME and nothing else. Timestamps
 * are excluded from the engine's identity compare, and the data
 * blocks are untouched, so the pair must classify hysterical --
 * via the BP_EQUAL tier, since the dnode itself was rewritten.
 */
int
rt_touch(objset_t *os, uint64_t obj)
{
	static uint64_t stamp = 1000000;
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	dmu_tx_t *tx;
	uint64_t mtime[2];
	int err;

	mtime[0] = ++stamp;
	mtime[1] = 0;

	err = sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_sa(tx, hdl, B_TRUE);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		sa_handle_destroy(hdl);
		return (err);
	}

	err = sa_update(hdl, sa_tbl[ZPL_MTIME], mtime, 16, tx);
	dmu_tx_commit(tx);
	sa_handle_destroy(hdl);

	return (err);
}

/*
 * Write a byte range at an arbitrary offset and set ZPL_SIZE to
 * newsize. Unlike rt_edit_file this can leave holes (create with
 * datalen 0, then write only past the start) and can fill them in
 * later with explicit bytes -- the H15 hole-vs-zeros fixture.
 */
int
rt_write_range(objset_t *os, uint64_t obj, uint64_t offset,
    const void *data, uint64_t len, uint64_t newsize)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	dmu_tx_t *tx;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, obj, offset, len);
	dmu_tx_hold_sa(tx, hdl, B_FALSE);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		sa_handle_destroy(hdl);
		return (err);
	}

	dmu_write(os, obj, offset, len, data, tx, 0);
	err = sa_update(hdl, sa_tbl[ZPL_SIZE], &newsize, 8, tx);
	dmu_tx_commit(tx);
	sa_handle_destroy(hdl);

	return (err);
}

/*
 * Set the SA-resident xattr form: pack a name -> byte-array nvlist
 * with NV_ENCODE_XDR (the encoding the ZPL uses for ZPL_DXATTR)
 * and store it. Pack order follows the nvlist's insertion order,
 * which is exactly what lets H21 build two byte-different packs of
 * the same logical set.
 */
int
rt_set_dxattr(objset_t *os, uint64_t obj, nvlist_t *xattrs)
{
	char *packed = NULL;
	size_t size = 0;
	int err;

	err = nvlist_size(xattrs, &size, NV_ENCODE_XDR);
	if (err != 0)
		return (err);

	packed = malloc(size);
	if (packed == NULL)
		return (ENOMEM);

	err = nvlist_pack(xattrs, &packed, &size, NV_ENCODE_XDR,
	    KM_SLEEP);
	if (err == 0)
		err = rt_set_sa_blob(os, obj, ZPL_DXATTR, packed,
		    (uint32_t)size);

	free(packed);
	return (err);
}

/*
 * Add one xattr in the hidden-directory form: get or create the
 * file's xattr directory (ZPL_XATTR points at it; it is a normal
 * DMU_OT_DIRECTORY_CONTENTS ZAP, unreachable from the root walk),
 * then store the value as a plain file inside it.
 */
int
rt_add_xattr_dir_entry(objset_t *os, uint64_t file_obj,
    const char *name, const void *value, uint64_t vlen)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	uint64_t xdir = 0;
	int err;

	err = sa_handle_get(os, file_obj, NULL, SA_HDL_SHARED, &hdl);
	if (err != 0)
		return (err);

	err = sa_lookup(hdl, sa_tbl[ZPL_XATTR], &xdir, 8);
	if (err == ENOENT) {
		dmu_tx_t *tx;

		tx = dmu_tx_create(os);
		dmu_tx_hold_sa(tx, hdl, B_TRUE);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
		dmu_tx_hold_sa_create(tx,
		    DN_BONUS_SIZE(DNODE_MIN_SIZE));
		err = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (err != 0) {
			dmu_tx_abort(tx);
			sa_handle_destroy(hdl);
			return (err);
		}

		xdir = zap_create_norm(os, 0,
		    DMU_OT_DIRECTORY_CONTENTS, DMU_OT_SA,
		    DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);
		err = set_sa_attrs(os, xdir, file_obj,
		    S_IFDIR | 0777, 2, tx);
		if (err == 0)
			err = sa_update(hdl, sa_tbl[ZPL_XATTR],
			    &xdir, 8, tx);
		dmu_tx_commit(tx);
	}
	sa_handle_destroy(hdl);
	if (err != 0)
		return (err);

	return (rt_create_file(os, xdir, name, value, vlen, NULL));
}

/*
 * Create a symlink: a plain-file dnode with S_IFLNK mode, the
 * target stored SA-resident in ZPL_SYMLINK (no NUL, matching the
 * ZPL), size = target length, no data blocks.
 */
int
rt_create_symlink(objset_t *os, uint64_t dir_obj, const char *name,
    const char *target, uint64_t *objp)
{
	dmu_tx_t *tx;
	uint64_t obj;
	uint64_t tlen = strlen(target);
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);
	dmu_tx_hold_sa_create(tx, DN_BONUS_SIZE(DNODE_MIN_SIZE));

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	obj = dmu_object_alloc(os, DMU_OT_PLAIN_FILE_CONTENTS, 0,
	    DMU_OT_SA, DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);

	err = set_sa_attrs(os, obj, dir_obj, S_IFLNK | 0777, tlen, tx);
	if (err == 0) {
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFLNK), obj);
		err = zap_add(os, dir_obj, name, 8, 1, &dirent, tx);
	}
	dmu_tx_commit(tx);
	if (err != 0)
		return (err);

	err = rt_set_sa_blob(os, obj, ZPL_SYMLINK, target,
	    (uint32_t)tlen);
	if (err != 0)
		return (err);

	if (objp != NULL)
		*objp = obj;
	return (0);
}

/*
 * Create a character-device node: S_IFCHR mode, ZPL_RDEV carrying
 * the device number, size 0, no data blocks.
 */
int
rt_create_device(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t rdev, uint64_t *objp)
{
	dmu_tx_t *tx;
	uint64_t obj;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, dir_obj, B_TRUE, name);
	dmu_tx_hold_sa_create(tx, DN_BONUS_SIZE(DNODE_MIN_SIZE));

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	obj = dmu_object_alloc(os, DMU_OT_PLAIN_FILE_CONTENTS, 0,
	    DMU_OT_SA, DN_BONUS_SIZE(DNODE_MIN_SIZE), tx);

	err = set_sa_attrs(os, obj, dir_obj, S_IFCHR | 0600, 0, tx);
	if (err == 0) {
		uint64_t dirent = ZFS_DIRENT_MAKE(IFTODT(S_IFCHR), obj);
		err = zap_add(os, dir_obj, name, 8, 1, &dirent, tx);
	}
	dmu_tx_commit(tx);
	if (err != 0)
		return (err);

	err = rt_set_sa_u64(os, obj, ZPL_RDEV, rdev);
	if (err != 0)
		return (err);

	if (objp != NULL)
		*objp = obj;
	return (0);
}

/*
 * Rename a file: remove from src_dir, add to dst_dir with same obj.
 */
int
rt_rename_file(objset_t *os, uint64_t src_dir, const char *old_name,
    uint64_t dst_dir, const char *new_name)
{
	uint64_t dirent;
	dmu_tx_t *tx;
	int err;

	/*
	 * Carry the raw dirent value over so the type nibble stays
	 * whatever the entry already was -- this renames dirs,
	 * symlinks, and devices as faithfully as files. Like a real
	 * ZPL rename at the namespace level; deliberately does NOT
	 * touch the renamed object's own dnode (no ctime), so a
	 * renamed-but-unedited object stays fast-path clean for the
	 * engine's untouched-since-fork check. Use rt_touch() when a
	 * fixture wants the dnode dirtied.
	 */
	err = zap_lookup(os, src_dir, old_name, 8, 1, &dirent);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, src_dir, B_FALSE, old_name);
	dmu_tx_hold_zap(tx, dst_dir, B_TRUE, new_name);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	VERIFY0(zap_remove(os, src_dir, old_name, tx));
	VERIFY0(zap_add(os, dst_dir, new_name, 8, 1, &dirent, tx));
	dmu_tx_commit(tx);
	return (0);
}

/*
 * ==== Post-apply inspection accessors (AP/CP matrices) ====
 *
 * Read the APPLIED left HEAD back so tests assert what apply
 * actually wrote, not merely that it returned success. These are
 * deliberately independent implementations -- they do not reuse
 * the engine's readers, so a bug shared with the engine cannot
 * hide itself.
 */

int
rt_get_sa_u64(objset_t *os, uint64_t obj, int zpl_attr, uint64_t *valp)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_PRIVATE, &hdl);
	if (err != 0)
		return (err);
	err = sa_lookup(hdl, sa_tbl[zpl_attr], valp, sizeof (uint64_t));
	sa_handle_destroy(hdl);
	return (err);
}

boolean_t
rt_sa_absent(objset_t *os, uint64_t obj, int zpl_attr)
{
	uint64_t v;

	return (rt_get_sa_u64(os, obj, zpl_attr, &v) == ENOENT);
}

boolean_t
rt_object_exists(objset_t *os, uint64_t obj)
{
	dmu_object_info_t doi;

	return (dmu_object_info(os, obj, &doi) == 0);
}

int
rt_read_data(objset_t *os, uint64_t obj, uint64_t off, uint64_t len,
    void *buf)
{
	return (dmu_read(os, obj, off, len, buf, DMU_READ_NO_PREFETCH));
}

int
rt_read_symlink(objset_t *os, uint64_t obj, char *buf, size_t buflen)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	int len;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_PRIVATE, &hdl);
	if (err != 0)
		return (err);
	err = sa_size(hdl, sa_tbl[ZPL_SYMLINK], &len);
	if (err == 0 && (size_t)len >= buflen)
		err = ERANGE;
	if (err == 0)
		err = sa_lookup(hdl, sa_tbl[ZPL_SYMLINK], buf, len);
	sa_handle_destroy(hdl);
	if (err == 0)
		buf[len] = '\0';
	return (err);
}

/*
 * Which physical xattr forms does this object carry? SA-resident
 * means a nonempty ZPL_DXATTR; directory means a nonzero
 * ZPL_XATTR (apply writes an explicit zero for "none").
 */
int
rt_xattr_forms(objset_t *os, uint64_t obj, boolean_t *sa_formp,
    boolean_t *dir_formp)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	uint64_t xd = 0;
	int sz = 0;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_PRIVATE, &hdl);
	if (err != 0)
		return (err);
	err = sa_size(hdl, sa_tbl[ZPL_DXATTR], &sz);
	if (err == ENOENT) {
		err = 0;
		sz = 0;
	}
	if (err == 0) {
		err = sa_lookup(hdl, sa_tbl[ZPL_XATTR], &xd,
		    sizeof (xd));
		if (err == ENOENT) {
			err = 0;
			xd = 0;
		}
	}
	sa_handle_destroy(hdl);
	if (err != 0)
		return (err);
	*sa_formp = (sz > 0);
	*dir_formp = (xd != 0);
	return (0);
}

/*
 * Logical xattr read: find one named value in whichever physical
 * form holds it (SA-resident first, then the hidden directory).
 * Copies at most buflen bytes and reports the full length.
 * Returns ENOENT when the name exists in neither form.
 */
int
rt_xattr_read(objset_t *os, uint64_t obj, const char *name, void *buf,
    uint_t buflen, uint_t *lenp)
{
	sa_attr_type_t *sa_tbl = get_sa_table(os);
	sa_handle_t *hdl;
	uint64_t xd = 0;
	int sz = 0;
	int err;

	err = sa_handle_get(os, obj, NULL, SA_HDL_PRIVATE, &hdl);
	if (err != 0)
		return (err);

	err = sa_size(hdl, sa_tbl[ZPL_DXATTR], &sz);
	if (err == 0 && sz > 0) {
		char *packed = kmem_alloc(sz, KM_SLEEP);
		nvlist_t *nvl = NULL;
		uchar_t *val;
		uint_t vlen;

		err = sa_lookup(hdl, sa_tbl[ZPL_DXATTR], packed, sz);
		if (err == 0)
			err = nvlist_unpack(packed, sz, &nvl, KM_SLEEP);
		kmem_free(packed, sz);
		if (err == 0 && nvlist_lookup_byte_array(nvl, name,
		    &val, &vlen) == 0) {
			memcpy(buf, val, MIN(vlen, buflen));
			*lenp = vlen;
			nvlist_free(nvl);
			sa_handle_destroy(hdl);
			return (0);
		}
		nvlist_free(nvl);
		if (err != 0) {
			sa_handle_destroy(hdl);
			return (err);
		}
	} else if (err == ENOENT) {
		err = 0;
	}

	if (err == 0) {
		err = sa_lookup(hdl, sa_tbl[ZPL_XATTR], &xd,
		    sizeof (xd));
		if (err == ENOENT) {
			err = 0;
			xd = 0;
		}
	}
	sa_handle_destroy(hdl);
	if (err != 0)
		return (err);
	if (xd != 0) {
		uint64_t child, vsz;

		err = rt_dir_lookup(os, xd, name, &child);
		if (err != 0)
			return (err);
		err = rt_get_sa_u64(os, child, ZPL_SIZE, &vsz);
		if (err != 0)
			return (err);
		if (vsz > 0)
			err = dmu_read(os, child, 0, MIN(vsz, buflen),
			    buf, DMU_READ_NO_PREFETCH);
		if (err == 0)
			*lenp = (uint_t)vsz;
		return (err);
	}
	return (ENOENT);
}
