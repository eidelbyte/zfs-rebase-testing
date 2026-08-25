// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Apply matrix (AP) tests plus the CP rows this pass flips -- see
 * TEST-MATRIX.md, "Apply matrix" and "Apply copy primitives
 * matrix". Each test's comment names its cells.
 *
 * These tests read the APPLIED left HEAD back: data, attributes,
 * xattr forms, link counts, object liveness. Until this section
 * existed, a copy that wrote garbage went green. The crash and
 * cancel tests drive the engine's injection tunables and MUST
 * reset them straight after the rebase call on every path.
 */

#include "rebase_test.h"

/*
 * CP1 (+CP21's file half): a copied file arrives byte-identical
 * with its attributes carried -- including a nonzero owner -- and
 * exactly one link.
 */
static int
test_apply_copy_file(void)
{
	rt_ds_t d;
	char pattern[1500], back[1500];
	uint64_t robj, obj = 0, mode = 0, uid = 0, links = 0, size = 0;
	int err, rerr = -1;
	nvlist_t *nvl;

	TEST_START("AP: copied file lands byte-identical");
	for (size_t i = 0; i < sizeof (pattern); i++)
		pattern[i] = (char)(i * 7);
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "cf", pattern,
	    sizeof (pattern), &robj);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, robj, ZPL_UID, 1234);
	rt_close(&d);
	RT_CHECK(err, "right create");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "cf", &obj);
	if (err == 0)
		err = rt_read_data(d.rtd_os, obj, 0,
		    sizeof (back), back);
	if (err == 0)
		rerr = memcmp(pattern, back, sizeof (pattern));
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_MODE, &mode);
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_UID, &uid);
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_LINKS, &links);
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_SIZE, &size);
	rt_close(&d);
	RT_CHECK(err, "inspect applied file");
	TEST_EXPECT(rerr == 0, "data mismatch");
	TEST_EXPECT(((mode >> 12) & 0xf) == 8, "not a regular file");
	TEST_EXPECT(uid == 1234, "owner not carried");
	TEST_EXPECT(links == 1, "wrong link count");
	TEST_EXPECT(size == sizeof (pattern), "wrong size");
	TEST_PASS();
}

/*
 * CP2: an empty file copies as an empty file.
 */
static int
test_apply_copy_empty(void)
{
	rt_ds_t d;
	uint64_t obj = 0, size = 1;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: empty file copies empty");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "ef", NULL, 0,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "right create");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "ef", &obj);
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_SIZE, &size);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(size == 0, "expected size 0");
	TEST_PASS();
}

/*
 * CP3: a right-added directory tree arrives whole: the directory
 * (a real ZAP with the directory mode nibble) and its child with
 * its content.
 */
static int
test_apply_copy_dir(void)
{
	rt_ds_t d;
	char back[6];
	uint64_t dobj = 0, cobj = 0, dmode = 0;
	int err, rerr = -1;
	nvlist_t *nvl;

	TEST_START("AP: directory tree copies whole");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "nd", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "inner",
		    "child", 5, NULL);
	rt_close(&d);
	RT_CHECK(err, "right tree");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "nd", &dobj);
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, dobj, ZPL_MODE, &dmode);
	if (err == 0)
		err = rt_dir_lookup(d.rtd_os, dobj, "inner", &cobj);
	if (err == 0)
		err = rt_read_data(d.rtd_os, cobj, 0, 5, back);
	if (err == 0)
		rerr = memcmp(back, "child", 5);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(((dmode >> 12) & 0xf) == 4, "not a directory");
	TEST_EXPECT(rerr == 0, "child data mismatch");
	TEST_PASS();
}

/*
 * CP4: a symlink's target string survives the copy.
 */
static int
test_apply_copy_symlink_sa(void)
{
	rt_ds_t d;
	char target[64];
	uint64_t obj = 0;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: symlink target survives the copy");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "ln",
	    "some/where", NULL);
	rt_close(&d);
	RT_CHECK(err, "right symlink");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "ln", &obj);
	if (err == 0)
		err = rt_read_symlink(d.rtd_os, obj, target,
		    sizeof (target));
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(strcmp(target, "some/where") == 0,
	    "target mismatch");
	TEST_PASS();
}

/*
 * CP6: a device node's rdev crosses.
 */
static int
test_apply_copy_device(void)
{
	rt_ds_t d;
	uint64_t obj = 0, rdev = 0;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: device node rdev crosses");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_device(d.rtd_os, d.rtd_root, "dev", 0x1234,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "right device");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "dev", &obj);
	if (err == 0)
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_RDEV, &rdev);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(rdev == 0x1234, "rdev mismatch");
	TEST_PASS();
}

/*
 * CP8: attributes the source never carried stay absent -- the
 * copy invents no defaults (PROJID, SYMLINK, DXATTR absent; XATTR
 * written as an explicit zero).
 */
static int
test_apply_no_invented_attrs(void)
{
	rt_ds_t d;
	uint64_t obj = 0, xd = 1;
	boolean_t projid_absent = B_FALSE, sym_absent = B_FALSE;
	boolean_t dx_absent = B_FALSE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: no invented attributes on the copy");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "plain", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "right create");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "plain", &obj);
	if (err == 0) {
		projid_absent = rt_sa_absent(d.rtd_os, obj,
		    ZPL_PROJID);
		sym_absent = rt_sa_absent(d.rtd_os, obj, ZPL_SYMLINK);
		dx_absent = rt_sa_absent(d.rtd_os, obj, ZPL_DXATTR);
		err = rt_get_sa_u64(d.rtd_os, obj, ZPL_XATTR, &xd);
	}
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(projid_absent, "PROJID invented");
	TEST_EXPECT(sym_absent, "SYMLINK invented");
	TEST_EXPECT(dx_absent, "DXATTR invented");
	TEST_EXPECT(xd == 0, "XATTR not the explicit zero");
	TEST_PASS();
}

/*
 * CP11: a source without xattrs applies with neither form.
 */
static int
test_apply_xattr_none(void)
{
	rt_ds_t d;
	uint64_t obj = 0;
	boolean_t sa_form = B_TRUE, dir_form = B_TRUE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: xattr-free source stays xattr-free");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "nf", "x", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "right create");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "nf", &obj);
	if (err == 0)
		err = rt_xattr_forms(d.rtd_os, obj, &sa_form,
		    &dir_form);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(!sa_form && !dir_form, "unexpected xattr form");
	TEST_PASS();
}

/*
 * CP14 + CP18: directory-form xattrs land in the directory form
 * when the destination says xattr=dir, values intact -- including
 * a zero-length value. The property is set EXPLICITLY: the
 * upstream default moved from dir to sa (zfs_prop.c registers
 * ZFS_XATTR_SA), and this test's first run failed by leaning on
 * the old default -- xattr fixtures never trust defaults again.
 */
static int
test_apply_xattr_dir_form(void)
{
	rt_ds_t d;
	char back[32];
	uint64_t robj, obj = 0;
	uint_t vlen = 0, elen = 1;
	boolean_t sa_form = B_TRUE, dir_form = B_FALSE;
	int err, rerr = -1, eerr = -1;
	nvlist_t *nvl;

	TEST_START("AP: dir-form xattrs land dir-form");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_set_dsl_prop_u64(RT_DS_LEFT, "xattr", 1),
	    "set xattr=dir");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "xf", "x", 1,
	    &robj);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, robj,
		    "user.a", "value-a", 7);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, robj,
		    "user.empty", "", 0);
	rt_close(&d);
	RT_CHECK(err, "right xattrs");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "xf", &obj);
	if (err == 0)
		err = rt_xattr_forms(d.rtd_os, obj, &sa_form,
		    &dir_form);
	if (err == 0) {
		rerr = rt_xattr_read(d.rtd_os, obj, "user.a", back,
		    sizeof (back), &vlen);
		eerr = rt_xattr_read(d.rtd_os, obj, "user.empty",
		    back, sizeof (back), &elen);
	}
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(!sa_form && dir_form, "expected dir form only");
	TEST_EXPECT(rerr == 0 && vlen == 7 &&
	    memcmp(back, "value-a", 7) == 0, "value mismatch");
	TEST_EXPECT(eerr == 0 && elen == 0, "empty value mismatch");
	TEST_PASS();
}

/*
 * CP13: with the destination at xattr=sa, a directory-form source
 * converts to SA-resident -- the representation is the
 * destination's, never the source's.
 */
static int
test_apply_xattr_dir_to_sa(void)
{
	rt_ds_t d;
	char back[32];
	uint64_t robj, obj = 0;
	uint_t vlen = 0;
	boolean_t sa_form = B_FALSE, dir_form = B_TRUE;
	int err, rerr = -1;
	nvlist_t *nvl;

	TEST_START("AP: dir-form source converts to sa-form");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_set_dsl_prop_u64(RT_DS_LEFT, "xattr", 2),
	    "set xattr=sa");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "xf", "x", 1,
	    &robj);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, robj,
		    "user.a", "value-a", 7);
	rt_close(&d);
	RT_CHECK(err, "right xattrs");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "xf", &obj);
	if (err == 0)
		err = rt_xattr_forms(d.rtd_os, obj, &sa_form,
		    &dir_form);
	if (err == 0)
		rerr = rt_xattr_read(d.rtd_os, obj, "user.a", back,
		    sizeof (back), &vlen);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(sa_form && !dir_form, "expected sa form only");
	TEST_EXPECT(rerr == 0 && vlen == 7 &&
	    memcmp(back, "value-a", 7) == 0, "value mismatch");
	TEST_PASS();
}

/*
 * CP16: under xattr=sa a value past the per-entry cap overflows
 * to the directory while a small value stays SA-resident -- both
 * forms on one object, both values intact.
 */
static int
test_apply_xattr_entry_overflow(void)
{
	rt_ds_t d;
	char *big, *back;
	uint64_t robj, obj = 0;
	uint_t vlen = 0, blen = 0;
	boolean_t sa_form = B_FALSE, dir_form = B_FALSE;
	int err, rerr = -1, berr = -1;
	nvlist_t *nvl;
	const uint_t bigsz = 40000;

	TEST_START("AP: oversized value overflows to dir form");
	big = kmem_alloc(bigsz, KM_SLEEP);
	back = kmem_alloc(bigsz, KM_SLEEP);
	for (uint_t i = 0; i < bigsz; i++)
		big[i] = (char)(i * 3);

	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_set_dsl_prop_u64(RT_DS_LEFT, "xattr", 2),
	    "set xattr=sa");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "xf", "x", 1,
	    &robj);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, robj,
		    "user.small", "little", 6);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, robj,
		    "user.big", big, bigsz);
	rt_close(&d);
	RT_CHECK(err, "right xattrs");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "xf", &obj);
	if (err == 0)
		err = rt_xattr_forms(d.rtd_os, obj, &sa_form,
		    &dir_form);
	if (err == 0) {
		rerr = rt_xattr_read(d.rtd_os, obj, "user.small",
		    back, bigsz, &vlen);
		if (rerr == 0 && (vlen != 6 ||
		    memcmp(back, "little", 6) != 0))
			rerr = -1;
		berr = rt_xattr_read(d.rtd_os, obj, "user.big",
		    back, bigsz, &blen);
		if (berr == 0 && (blen != bigsz ||
		    memcmp(back, big, bigsz) != 0))
			berr = -1;
	}
	rt_close(&d);
	kmem_free(big, bigsz);
	kmem_free(back, bigsz);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(sa_form && dir_form,
	    "expected BOTH forms (overflow)");
	TEST_EXPECT(rerr == 0, "small value wrong");
	TEST_EXPECT(berr == 0, "big value wrong");
	TEST_PASS();
}

/*
 * CP19 + CP20: with the destination at xattr=off, an xattr-free
 * addition applies cleanly, but an xattr-carrying one refuses
 * with EOPNOTSUPP, the apply rolls back (neither file lands),
 * and the fence survives. The v1 policy is refusal, never a
 * silent drop.
 */
static int
test_apply_xattr_off(void)
{
	rt_ds_t d;
	uint64_t robj, obj;
	boolean_t plain_gone = B_FALSE, xf_gone = B_FALSE;
	boolean_t fence = B_FALSE;
	int err;

	TEST_START("AP: xattr=off refuses and rolls back");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_set_dsl_prop_u64(RT_DS_LEFT, "xattr", 0),
	    "set xattr=off");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "plain", "p", 1,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "xf",
		    "x", 1, &robj);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, robj,
		    "user.a", "v", 1);
	rt_close(&d);
	RT_CHECK(err, "right files");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	TEST_EXPECT(err == EOPNOTSUPP,
	    "expected EOPNOTSUPP from xattr=off");

	fence = rt_fence_exists();
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	plain_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "plain",
	    &obj) == ENOENT);
	xf_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "xf",
	    &obj) == ENOENT);
	rt_close(&d);
	TEST_EXPECT(plain_gone && xf_gone,
	    "rollback did not restore the HEAD");
	TEST_EXPECT(fence, "fence missing after failure");
	TEST_PASS();
}

/*
 * AP1: a deleted standalone file is gone -- dirent and object.
 */
static int
test_apply_unlink_file(void)
{
	rt_ds_t d;
	uint64_t bobj, obj;
	boolean_t dirent_gone = B_FALSE, obj_gone = B_FALSE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: deleted file fully gone");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "gone", "g", 1,
	    &bobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "gone");
	rt_close(&d);
	RT_CHECK(err, "right delete");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	dirent_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "gone",
	    &obj) == ENOENT);
	obj_gone = !rt_object_exists(d.rtd_os, bobj);
	rt_close(&d);
	TEST_EXPECT(dirent_gone, "dirent survived");
	TEST_EXPECT(obj_gone, "object not freed");
	TEST_PASS();
}

/*
 * AP2: a deleted directory tree empties bottom-up and every
 * object is unallocated afterward.
 */
static int
test_apply_unlink_tree(void)
{
	rt_ds_t d;
	uint64_t dobj, cobj, obj;
	boolean_t dir_gone = B_FALSE, dobj_gone = B_FALSE;
	boolean_t cobj_gone = B_FALSE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: deleted tree fully gone");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "dd", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "inner", "i",
		    1, &cobj);
	rt_close(&d);
	RT_CHECK(err, "base tree");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, dobj, "inner");
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "dd");
	rt_close(&d);
	RT_CHECK(err, "right tree delete");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	dir_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "dd",
	    &obj) == ENOENT);
	dobj_gone = !rt_object_exists(d.rtd_os, dobj);
	cobj_gone = !rt_object_exists(d.rtd_os, cobj);
	rt_close(&d);
	TEST_EXPECT(dir_gone, "dir dirent survived");
	TEST_EXPECT(dobj_gone, "dir object not freed");
	TEST_EXPECT(cobj_gone, "child object not freed");
	TEST_PASS();
}

/*
 * AP3: unlinking one pool member removes the name, counts the
 * link down, and leaves the survivor intact.
 */
static int
test_apply_unlink_pool_member(void)
{
	rt_ds_t d;
	char back[1];
	uint64_t pobj, obj, links = 0;
	boolean_t a_gone = B_FALSE, alive = B_FALSE;
	int err, rerr = -1;
	nvlist_t *nvl;

	TEST_START("AP: pool member unlink counts down");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "p", 1,
	    &pobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B",
		    pobj);
	rt_close(&d);
	RT_CHECK(err, "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "A");
	rt_close(&d);
	RT_CHECK(err, "right unlink");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	a_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "A",
	    &obj) == ENOENT);
	alive = rt_object_exists(d.rtd_os, pobj);
	err = rt_get_sa_u64(d.rtd_os, pobj, ZPL_LINKS, &links);
	if (err == 0)
		rerr = rt_read_data(d.rtd_os, pobj, 0, 1, back);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(a_gone, "unlinked name survived");
	TEST_EXPECT(alive, "object wrongly freed");
	TEST_EXPECT(links == 1, "wrong link count");
	TEST_EXPECT(rerr == 0 && back[0] == 'p', "survivor data");
	TEST_PASS();
}

/*
 * AP4: a dead pool (every member unlinked, left silent, no edit)
 * loses its object.
 */
static int
test_apply_unlink_dead_pool(void)
{
	rt_ds_t d;
	uint64_t pobj, obj;
	boolean_t a_gone = B_FALSE, b_gone = B_FALSE;
	boolean_t obj_gone = B_FALSE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: dead pool frees its object");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "p", 1,
	    &pobj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B",
		    pobj);
	rt_close(&d);
	RT_CHECK(err, "base pool");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "A");
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "B");
	rt_close(&d);
	RT_CHECK(err, "right unlinks");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	a_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "A",
	    &obj) == ENOENT);
	b_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "B",
	    &obj) == ENOENT);
	obj_gone = !rt_object_exists(d.rtd_os, pobj);
	rt_close(&d);
	TEST_EXPECT(a_gone && b_gone, "names survived");
	TEST_EXPECT(obj_gone, "dead pool object not freed");
	TEST_PASS();
}

/*
 * AP5 (CP22's delete half): deleting an xattr-carrying file takes
 * the hidden directory and its value child with it.
 */
static int
test_apply_unlink_xattr_file(void)
{
	rt_ds_t d;
	uint64_t bobj, xd = 0, child = 0, obj;
	boolean_t f_gone = B_FALSE, xd_gone = B_FALSE;
	boolean_t child_gone = B_FALSE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: deleted xattr file frees its satellite");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "xf", "x", 1,
	    &bobj);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, bobj,
		    "user.a", "v", 1);
	rt_close(&d);
	RT_CHECK(err, "base xattr file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_get_sa_u64(d.rtd_os, bobj, ZPL_XATTR, &xd);
	if (err == 0 && xd != 0)
		err = rt_dir_lookup(d.rtd_os, xd, "user.a", &child);
	rt_close(&d);
	RT_CHECK(err, "map satellite");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "xf");
	rt_close(&d);
	RT_CHECK(err, "right delete");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	f_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "xf",
	    &obj) == ENOENT);
	xd_gone = !rt_object_exists(d.rtd_os, xd);
	child_gone = !rt_object_exists(d.rtd_os, child);
	rt_close(&d);
	TEST_EXPECT(f_gone, "file survived");
	TEST_EXPECT(xd_gone, "xattr dir not freed");
	TEST_EXPECT(child_gone, "xattr child not freed");
	TEST_PASS();
}

/*
 * AP6: the LINK-phase handoff. A right-side rename applies its
 * UNLINK now and its LINK later, so between the phases the old
 * name is gone, the new name is not yet present, and the object
 * waits with a zero link count. THIS CELL'S EXPECTATION CHANGES
 * when apply-structural lands (new name present, links 1) -- the
 * assertions below must be rewritten then, not patched around.
 */
static int
test_apply_move_handoff(void)
{
	rt_ds_t d;
	uint64_t bobj, obj, links = 111;
	boolean_t old_gone = B_FALSE, new_absent = B_FALSE;
	boolean_t alive = B_FALSE;
	int err;
	nvlist_t *nvl;

	TEST_START("AP: move handoff parks the object");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "m", "m", 1,
	    &bobj);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_rename_file(d.rtd_os, d.rtd_root, "m", d.rtd_root,
	    "m2");
	rt_close(&d);
	RT_CHECK(err, "right rename");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	old_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "m",
	    &obj) == ENOENT);
	new_absent = (rt_dir_lookup(d.rtd_os, d.rtd_root, "m2",
	    &obj) == ENOENT);
	alive = rt_object_exists(d.rtd_os, bobj);
	err = rt_get_sa_u64(d.rtd_os, bobj, ZPL_LINKS, &links);
	rt_close(&d);
	RT_CHECK(err, "inspect");
	TEST_EXPECT(old_gone, "old name survived");
	TEST_EXPECT(new_absent, "new name already present "
	    "(apply-structural landed? rewrite this cell)");
	TEST_EXPECT(alive, "moved object freed");
	TEST_EXPECT(links == 0, "expected parked link count 0");
	TEST_PASS();
}

/*
 * AP7: USER CANCEL. Three copies pending, injected stop after
 * one: EINTR, the automatic rollback restores the pre-state
 * completely, the fence survives.
 */
static int
test_apply_cancel_rollback(void)
{
	rt_ds_t d;
	uint64_t obj;
	boolean_t f1_gone = B_FALSE, f2_gone = B_FALSE;
	boolean_t f3_gone = B_FALSE, fence = B_FALSE;
	int err;

	TEST_START("AP: cancel rolls all of it back");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f1", "1", 1,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "f2",
		    "2", 1, NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "f3",
		    "3", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "right creates");

	rt_sync_pool();
	rt_apply_inject(1, 0);
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_apply_inject(0, 0);
	TEST_EXPECT(err == EINTR, "expected EINTR from injection");

	fence = rt_fence_exists();
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	f1_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "f1",
	    &obj) == ENOENT);
	f2_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "f2",
	    &obj) == ENOENT);
	f3_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "f3",
	    &obj) == ENOENT);
	rt_close(&d);
	TEST_EXPECT(f1_gone && f2_gone && f3_gone,
	    "rollback left additions behind");
	TEST_EXPECT(fence, "fence missing after cancel");
	TEST_PASS();
}

/*
 * AP8: CRASH. Same fixture, stop after one with the rollback
 * suppressed: the HEAD is left PARTIAL -- exactly the first
 * action's file, which also pins list-order determinism -- with
 * the fence intact. A manual rollback to the fence (the abort
 * flow's move, rehearsed here) then restores the pre-state.
 */
static int
test_apply_crash_partial(void)
{
	rt_ds_t d;
	uint64_t obj;
	boolean_t f1_there = B_FALSE, f2_gone = B_FALSE;
	boolean_t fence = B_FALSE, f1_gone_after = B_FALSE;
	int err;

	TEST_START("AP: crash leaves partial; fence recovers");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f1", "1", 1,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "f2",
		    "2", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "right creates");

	rt_sync_pool();
	rt_apply_inject(1, 1);
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_apply_inject(0, 0);
	TEST_EXPECT(err == EINTR, "expected EINTR from injection");

	fence = rt_fence_exists();
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	f1_there = (rt_dir_lookup(d.rtd_os, d.rtd_root, "f1",
	    &obj) == 0);
	f2_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "f2",
	    &obj) == ENOENT);
	rt_close(&d);
	TEST_EXPECT(f1_there, "first action's file missing");
	TEST_EXPECT(f2_gone, "second action applied past the stop");
	TEST_EXPECT(fence, "fence missing after crash");

	rt_sync_pool();
	RT_CHECK(rt_rollback_to_fence(), "manual rollback failed");
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	f1_gone_after = (rt_dir_lookup(d.rtd_os, d.rtd_root, "f1",
	    &obj) == ENOENT);
	rt_close(&d);
	TEST_EXPECT(f1_gone_after, "recovery did not restore");
	TEST_EXPECT(rt_fence_exists(), "fence lost by recovery");
	TEST_PASS();
}

/*
 * AP9: a cancel landing in the SECOND pass -- the copy applied,
 * the unlink loop stopped -- still rolls both back: the added
 * file vanishes and the deleted file returns.
 */
static int
test_apply_cancel_unlinks(void)
{
	rt_ds_t d;
	uint64_t obj;
	boolean_t nf_gone = B_FALSE, gone_back = B_FALSE;
	int err;

	TEST_START("AP: cancel in the unlink pass rolls back");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "gone", "g", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "nf", "n", 1,
	    NULL);
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "gone");
	rt_close(&d);
	RT_CHECK(err, "right changes");

	rt_sync_pool();
	rt_apply_inject(1, 0);
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	rt_apply_inject(0, 0);
	TEST_EXPECT(err == EINTR, "expected EINTR from injection");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	nf_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "nf",
	    &obj) == ENOENT);
	gone_back = (rt_dir_lookup(d.rtd_os, d.rtd_root, "gone",
	    &obj) == 0);
	rt_close(&d);
	TEST_EXPECT(nf_gone, "copied file survived the rollback");
	TEST_EXPECT(gone_back, "deleted file not restored");
	TEST_PASS();
}

/*
 * AP10: the fence is the revert point -- after a fully successful
 * apply it still exists and reads as the pre-apply state.
 */
static int
test_apply_fence_content(void)
{
	rt_ds_t d;
	char back[1];
	uint64_t obj;
	boolean_t gone_in_head = B_FALSE, added_in_head = B_FALSE;
	boolean_t gone_in_fence = B_FALSE, added_absent = B_FALSE;
	int err, rerr = -1;
	nvlist_t *nvl;

	TEST_START("AP: fence reads as the pre-apply state");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "gone", "g", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "nf", "n", 1,
	    NULL);
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "gone");
	rt_close(&d);
	RT_CHECK(err, "right changes");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "rebase failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	gone_in_head = (rt_dir_lookup(d.rtd_os, d.rtd_root, "gone",
	    &obj) == ENOENT);
	added_in_head = (rt_dir_lookup(d.rtd_os, d.rtd_root, "nf",
	    &obj) == 0);
	rt_close(&d);
	TEST_EXPECT(gone_in_head && added_in_head,
	    "apply did not land");

	RT_CHECK(rt_open_snap(RT_DS_LEFT "@" ZFS_REBASE_SNAP_SUFFIX,
	    &d), "hold fence");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "gone", &obj);
	gone_in_fence = (err == 0);
	if (err == 0)
		rerr = rt_read_data(d.rtd_os, obj, 0, 1, back);
	added_absent = (rt_dir_lookup(d.rtd_os, d.rtd_root, "nf",
	    &obj) == ENOENT);
	rt_close_snap(&d);
	TEST_EXPECT(gone_in_fence && rerr == 0 && back[0] == 'g',
	    "fence lost the deleted file");
	TEST_EXPECT(added_absent, "fence contains post-apply state");
	TEST_PASS();
}

/*
 * AP11 (CP23): a REAL mid-apply failure -- a corrupt DXATTR blob
 * on the copy source -- comes back EIO with the HEAD rolled back
 * and the fence intact. Apply is the gatherer's third caller;
 * H36 and U9 pinned the other two.
 */
static int
test_apply_corrupt_xattr_source(void)
{
	rt_ds_t d;
	uint64_t robj, obj;
	boolean_t nf_gone = B_FALSE, fence = B_FALSE;
	int err;
	static const char garbage[] = "not an nvlist at all";

	TEST_START("AP: corrupt source xattr fails and rolls back");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");
	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "nf", "n", 1,
	    &robj);
	if (err == 0)
		err = rt_set_sa_blob(d.rtd_os, robj, ZPL_DXATTR,
		    garbage, sizeof (garbage));
	rt_close(&d);
	RT_CHECK(err, "right corrupt file");

	rt_sync_pool();
	err = dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL);
	TEST_EXPECT(err == EIO, "expected EIO from corrupt DXATTR");

	fence = rt_fence_exists();
	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	nf_gone = (rt_dir_lookup(d.rtd_os, d.rtd_root, "nf",
	    &obj) == ENOENT);
	rt_close(&d);
	TEST_EXPECT(nf_gone, "rollback did not restore the HEAD");
	TEST_EXPECT(fence, "fence missing after failure");
	TEST_PASS();
}

/*
 * AP12: the tally line's arithmetic: two copies, one unlink, one
 * deferred WRITE.
 */
static int
test_apply_stats_line(void)
{
	rt_ds_t d;
	uint64_t bobj, copies = 0, unlinks = 0, deferred = 0;
	int err, serr = -1;
	nvlist_t *nvl;

	TEST_START("AP: tally line arithmetic");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "gone", "g", 1,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "ed",
		    "e", 1, &bobj);
	rt_close(&d);
	RT_CHECK(err, "base files");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "n1", "1", 1,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "n2",
		    "2", 1, NULL);
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "gone");
	if (err == 0)
		err = rt_edit_file(d.rtd_os, bobj, "E", 1);
	rt_close(&d);
	RT_CHECK(err, "right changes");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0) {
		fnvlist_free(nvl);
		serr = rt_apply_stats(&copies, &unlinks, &deferred);
	}
	RT_CHECK(err, "rebase failed");
	RT_CHECK(serr, "no tally line");
	TEST_EXPECT(copies == 2, "wrong copies");
	TEST_EXPECT(unlinks == 1, "wrong unlinks");
	TEST_EXPECT(deferred == 1, "wrong deferred");
	TEST_PASS();
}

/*
 * AP13 (CP24): the acceptance round trip. Apply, clear the fence
 * (the first fence-lifecycle exercise, rehearsing finish/abort),
 * and rebase again: the second run must see nothing to do --
 * zero conflicts, zero copies, zero unlinks. The applied result
 * is invisible to the engine's own diff.
 */
static int
test_apply_roundtrip_acceptance(void)
{
	rt_ds_t d;
	uint64_t copies = 9, unlinks = 9, deferred = 0;
	int err, serr = -1;
	nvlist_t *nvl;

	TEST_START("AP: acceptance -- re-rebase sees silence");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");
	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "gone", "g", 1,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "base file");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "nf", "fresh",
	    5, NULL);
	if (err == 0)
		err = rt_remove_entry(d.rtd_os, d.rtd_root, "gone");
	rt_close(&d);
	RT_CHECK(err, "right changes");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err == 0)
		fnvlist_free(nvl);
	RT_CHECK(err, "first rebase failed");

	RT_CHECK(rt_destroy_fence(), "fence clear failed");
	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	RT_CHECK(err, "second rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "second run found conflicts");
	fnvlist_free(nvl);
	serr = rt_apply_stats(&copies, &unlinks, &deferred);
	RT_CHECK(serr, "no tally line");
	TEST_EXPECT(copies == 0 && unlinks == 0,
	    "second run had work to do");
	TEST_PASS();
}

void
run_apply_tests(void)
{
	(void) printf("\n[apply: applied-state inspection (AP/CP)]\n");
	(void) test_apply_copy_file();
	(void) test_apply_copy_empty();
	(void) test_apply_copy_dir();
	(void) test_apply_copy_symlink_sa();
	(void) test_apply_copy_device();
	(void) test_apply_no_invented_attrs();
	(void) test_apply_xattr_none();
	(void) test_apply_xattr_dir_form();
	(void) test_apply_xattr_dir_to_sa();
	(void) test_apply_xattr_entry_overflow();
	(void) test_apply_xattr_off();
	(void) test_apply_unlink_file();
	(void) test_apply_unlink_tree();
	(void) test_apply_unlink_pool_member();
	(void) test_apply_unlink_dead_pool();
	(void) test_apply_unlink_xattr_file();
	(void) test_apply_move_handoff();
	(void) test_apply_cancel_rollback();
	(void) test_apply_crash_partial();
	(void) test_apply_cancel_unlinks();
	(void) test_apply_fence_content();
	(void) test_apply_corrupt_xattr_source();
	(void) test_apply_stats_line();
	(void) test_apply_roundtrip_acceptance();
}
