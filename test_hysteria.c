// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Hysteria matrix (H) tests -- see TEST-MATRIX.md, "Hysteria matrix".
 * Each test's comment names the cell(s) it covers.
 *
 * Until standalone-diff emits changelists, classification is
 * asserted through the engine's walk-summary dbgmsg counters
 * (visited, hysterical left/right, linkpool-member paths) via
 * rt_walk_stats(). Every fixture edits the LEFT side unless the
 * test says otherwise; H35 pins counter side-symmetry.
 *
 * The trailing section keeps the sprint-1-era suppression tests
 * that assert crossref-era manifest behavior; they fail cleanly
 * through the defensive accessors until cross-reference lands and
 * will be re-plotted into the crossref matrix.
 */

#include "rebase_test.h"

#define	RT_128K		131072ULL
#define	RT_384K		393216ULL

/*
 * Sync, run the rebase (expecting success),
 * scrape the walk summary, tear the pool down, and compare all
 * four counters. Returns 0 on match; prints the mismatch and
 * returns nonzero otherwise. Callers wrap it in TEST_FAIL/PASS.
 */
static int
hyst_finish(uint64_t ev, uint64_t ehl, uint64_t ehr, uint64_t elk)
{
	nvlist_t *nvl;
	rt_walk_stats_t ws;
	int err;

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	if (err != 0) {
		rt_scaffold_teardown();
		(void) printf("\n    [hyst] rebase failed: %s (%d)\n",
		    strerror(err), err);
		return (1);
	}
	fnvlist_free(nvl);

	err = rt_walk_stats(&ws);
	rt_scaffold_teardown();
	if (err != 0) {
		(void) printf("\n    [hyst] no walk summary line "
		    "(%d)\n", err);
		return (1);
	}

	if (ws.rws_visited != ev || ws.rws_hyst_left != ehl ||
	    ws.rws_hyst_right != ehr || ws.rws_linked != elk) {
		(void) printf("\n    [hyst] expected v=%llu hl=%llu "
		    "hr=%llu lk=%llu, got v=%llu hl=%llu hr=%llu "
		    "lk=%llu\n",
		    (unsigned long long)ev, (unsigned long long)ehl,
		    (unsigned long long)ehr, (unsigned long long)elk,
		    (unsigned long long)ws.rws_visited,
		    (unsigned long long)ws.rws_hyst_left,
		    (unsigned long long)ws.rws_hyst_right,
		    (unsigned long long)ws.rws_linked);
		return (1);
	}
	return (0);
}

/*
 * H1: nothing edited anywhere. Every same-path pair (two files and
 * a directory) resolves hysterical via the fork-txg fast path, and
 * the visited count pins the union size.
 */
static int
test_hysteria_untouched_fast_path(void)
{
	TEST_START("H1: untouched since fork (fast path)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");
	if (hyst_finish(3, 3, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H2: two files created back to back (adjacent object numbers, so
 * almost surely one shared dnode block); only the neighbor is
 * edited. The untouched file's fast path sees a dirty dnode block
 * and must fall through the content tiers to the same answer --
 * the assertion holds whichever path runs.
 */
static int
test_hysteria_neighbor_churn(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H2: dnode-block neighbor churn");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "fa", "aaaa", 4,
	    NULL);
	if (err == 0)
		err = rt_create_file(d.rtd_os, d.rtd_root, "fb",
		    "bbbb", 4, NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "fb", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "BBBB", 4);
	rt_close(&d);
	RT_CHECK(err, "edit neighbor");

	if (hyst_finish(2, 1, 2, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H3: timestamps-only update. MTIME is excluded from identity and
 * the data blocks are carried verbatim into the rewritten dnode,
 * so the pair resolves hysterical through the BP_EQUAL tier.
 */
static int
test_hysteria_touch_only(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H3: touch(1)-style timestamp-only update");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_touch(d.rtd_os, obj);
	rt_close(&d);
	RT_CHECK(err, "touch");

	if (hyst_finish(3, 3, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H4 + H5: each fixed SA identity attribute flipped in turn on its
 * own file -- mode (the fast-path soundness guard: a bonus-only
 * edit must never classify untouched), uid, gid, flags, and projid
 * (absent at creation, so this one also exercises the
 * present-vs-absent rule). None may classify hysterical.
 */
static int
test_hysteria_sa_identity(void)
{
	static const char *names[] = { "fm", "fu", "fg", "ff", "fp" };
	rt_ds_t d;
	uint64_t objs[5];
	int err = 0;

	TEST_START("H4+H5: SA identity attrs flipped in turn");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	for (int i = 0; i < 5 && err == 0; i++)
		err = rt_create_file(d.rtd_os, d.rtd_root, names[i],
		    "x", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	for (int i = 0; i < 5 && err == 0; i++)
		err = rt_dir_lookup(d.rtd_os, d.rtd_root, names[i],
		    &objs[i]);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, objs[0], ZPL_MODE,
		    S_IFREG | 0600);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, objs[1], ZPL_UID, 5);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, objs[2], ZPL_GID, 5);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, objs[3], ZPL_FLAGS, 1);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, objs[4], ZPL_PROJID, 7);
	rt_close(&d);
	RT_CHECK(err, "flip attrs");

	if (hyst_finish(5, 0, 5, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H6: DACL_ACES present on one side only. The engine compares the
 * blob by memcmp, so validity does not matter; present-vs-absent
 * differs.
 */
static int
test_hysteria_acl(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H6: ACL blob on one side only");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0)
		err = rt_set_sa_blob(d.rtd_os, obj, ZPL_DACL_ACES,
		    "\x01\x02\x03\x04", 4);
	rt_close(&d);
	RT_CHECK(err, "set acl blob");

	if (hyst_finish(1, 0, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H7: recycled-slot simulation -- same object number, identical
 * content, but ZPL_GEN flipped. The recycling guard must classify
 * it as not hysterical (ADD+DELETE lineage break), no matter what
 * the content says.
 */
static int
test_hysteria_recycled_gen(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H7: gen flip = recycled slot, never an edit");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, obj, ZPL_GEN, 424242);
	rt_close(&d);
	RT_CHECK(err, "flip gen");

	if (hyst_finish(3, 2, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H8: rename-on-save (nvim-style) -- a NEW dnode under the old
 * path with the same content and identity. Hysterical.
 */
static int
test_hysteria_rename_on_save(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H8: rename-on-save, content unchanged");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical edit");

	if (hyst_finish(3, 3, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H9: new dnode under the old path with DIFFERENT content: a real
 * edit through the same mechanism H8 uses.
 */
static int
test_hysteria_recreate_differs(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H9: recreate with different content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "different!\n", 11, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate");

	if (hyst_finish(3, 2, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H10: same dnode rewritten with identical bytes. New block
 * pointers, same data; on this fletcher pool the checksum is not
 * provable, so tier 3's byte compare decides. Hysterical.
 */
static int
test_hysteria_rewrite_identical(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H10: in-place rewrite, identical bytes");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "world\n", 6);
	rt_close(&d);
	RT_CHECK(err, "rewrite");

	if (hyst_finish(3, 3, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H11: same dnode rewritten with different bytes of the same
 * length (size cannot catch it; the byte compare must).
 */
static int
test_hysteria_rewrite_differs(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H11: in-place rewrite, different bytes");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "WORLD\n", 6);
	rt_close(&d);
	RT_CHECK(err, "rewrite");

	if (hyst_finish(3, 2, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H12: empty file on both sides, touched so the pair escapes the
 * fast path and exercises the size-0 short circuit in the data
 * tier.
 */
static int
test_hysteria_empty_files(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H12: empty file, size-0 short circuit");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "empty", "", 0,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "empty", &obj);
	if (err == 0)
		err = rt_touch(d.rtd_os, obj);
	rt_close(&d);
	RT_CHECK(err, "touch");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H13: append -- size differs, identical prefix. The SA identity
 * compare (ZPL_SIZE) must catch it before any data read.
 */
static int
test_hysteria_append(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H13: append (size differs)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "world\nplus\n", 11);
	rt_close(&d);
	RT_CHECK(err, "append");

	if (hyst_finish(3, 2, 3, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H14: multi-block file (3 x 128K) rewritten with identical bytes:
 * the tier-3 loop must cross chunk boundaries and still conclude
 * equal.
 */
static int
test_hysteria_multiblock(void)
{
	rt_ds_t d;
	uint64_t obj;
	char *buf;
	int err;

	TEST_START("H14: multi-block identical rewrite");

	buf = malloc(RT_384K);
	if (buf == NULL)
		TEST_FAIL("malloc");
	for (uint64_t i = 0; i < RT_384K; i++)
		buf[i] = (char)(i & 0xff);

	if (rt_scaffold_empty_base() != 0) {
		free(buf);
		TEST_FAIL("scaffold failed");
	}

	err = rt_open(RT_DS_SRC, &d);
	if (err == 0) {
		err = rt_create_file(d.rtd_os, d.rtd_root, "big",
		    buf, RT_384K, NULL);
		rt_close(&d);
	}
	if (err == 0)
		err = rt_scaffold_snap_and_clone();
	if (err == 0)
		err = rt_open(RT_DS_LEFT, &d);
	if (err == 0) {
		err = rt_dir_lookup(d.rtd_os, d.rtd_root, "big", &obj);
		if (err == 0)
			err = rt_edit_file(d.rtd_os, obj, buf,
			    RT_384K);
		rt_close(&d);
	}
	free(buf);
	RT_CHECK(err, "big-file fixture");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H15: hole in base vs explicit zeros on the side, same logical
 * bytes. Tier 2 must fall through on the hole-vs-data pair and
 * tier 3 must read the hole as zeros and conclude equal.
 */
static int
test_hysteria_hole_vs_zeros(void)
{
	rt_ds_t d;
	uint64_t obj;
	char *pat, *zeros;
	int err;

	TEST_START("H15: hole vs written zeros");

	pat = malloc(RT_128K);
	zeros = calloc(1, RT_128K);
	if (pat == NULL || zeros == NULL) {
		free(pat);
		free(zeros);
		TEST_FAIL("malloc");
	}
	for (uint64_t i = 0; i < RT_128K; i++)
		pat[i] = (char)(0x40 | (i & 0x1f));

	if (rt_scaffold_empty_base() != 0) {
		free(pat);
		free(zeros);
		TEST_FAIL("scaffold failed");
	}

	err = rt_open(RT_DS_SRC, &d);
	if (err == 0) {
		err = rt_create_file(d.rtd_os, d.rtd_root, "sparse",
		    "", 0, &obj);
		if (err == 0)
			err = rt_write_range(d.rtd_os, obj, RT_128K,
			    pat, RT_128K, 2 * RT_128K);
		rt_close(&d);
	}
	if (err == 0)
		err = rt_scaffold_snap_and_clone();
	if (err == 0)
		err = rt_open(RT_DS_LEFT, &d);
	if (err == 0) {
		err = rt_dir_lookup(d.rtd_os, d.rtd_root, "sparse",
		    &obj);
		if (err == 0)
			err = rt_write_range(d.rtd_os, obj, 0, zeros,
			    RT_128K, 2 * RT_128K);
		rt_close(&d);
	}
	free(pat);
	free(zeros);
	RT_CHECK(err, "sparse fixture");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H18: a lone directory, untouched: the dir pair itself must
 * classify hysterical via the fast path.
 */
static int
test_hysteria_dir_untouched(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H18: untouched directory");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "d", NULL);
	rt_close(&d);
	RT_CHECK(err, "mkdir");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H19: the ZPL_SIZE-skip rule. A child is added on the left and
 * the dir's entry-count size is bumped to match real ZPL behavior
 * (the harness does not maintain it automatically). The directory
 * itself must stay hysterical -- the child add is the child's own
 * record -- so hl counts the dir AND the untouched sibling.
 */
static int
test_hysteria_dir_entries(void)
{
	rt_ds_t d;
	uint64_t dobj;
	int err;

	TEST_START("H19: dir with entries changed stays hysterical");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "f1", "x", 1,
		    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "f2", "y", 1,
		    NULL);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, dobj, ZPL_SIZE, 3);
	rt_close(&d);
	RT_CHECK(err, "add child");

	if (hyst_finish(3, 2, 2, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H20: directory chmod is a real edit -- mode is identity for
 * directories too.
 */
static int
test_hysteria_dir_chmod(void)
{
	rt_ds_t d;
	uint64_t dobj;
	int err;

	TEST_START("H20: dir chmod is a real edit");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_dir(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_create_file(d.rtd_os, dobj, "f1", "x", 1,
		    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "d", &dobj);
	if (err == 0)
		err = rt_set_sa_u64(d.rtd_os, dobj, ZPL_MODE,
		    S_IFDIR | 0700);
	rt_close(&d);
	RT_CHECK(err, "chmod dir");

	if (hyst_finish(2, 1, 2, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * Build a one- or two-entry xattr nvlist. Order of insertion
 * controls pack order (H21's lever).
 */
static nvlist_t *
hyst_xattrs(const char *n1, const char *v1, const char *n2,
    const char *v2)
{
	nvlist_t *nvl = fnvlist_alloc();

	fnvlist_add_byte_array(nvl, n1, (const uchar_t *)v1,
	    (uint_t)strlen(v1));
	if (n2 != NULL)
		fnvlist_add_byte_array(nvl, n2, (const uchar_t *)v2,
		    (uint_t)strlen(v2));
	return (nvl);
}

/*
 * H21: same logical SA-form xattr set on both sides, packed in
 * different orders (byte-different DXATTR blobs). Logical compare
 * must call it hysterical.
 */
static int
test_hysteria_xattr_sa_equal(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("H21: SA xattrs equal, different pack order");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, &obj);
	if (err == 0) {
		nvl = hyst_xattrs("user.a", "1", "user.b", "2");
		err = rt_set_dxattr(d.rtd_os, obj, nvl);
		fnvlist_free(nvl);
	}
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0) {
		nvl = hyst_xattrs("user.b", "2", "user.a", "1");
		err = rt_set_dxattr(d.rtd_os, obj, nvl);
		fnvlist_free(nvl);
	}
	rt_close(&d);
	RT_CHECK(err, "repack xattrs");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H22: SA-form xattr value differs: a real edit.
 */
static int
test_hysteria_xattr_value(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("H22: xattr value differs");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, &obj);
	if (err == 0) {
		nvl = hyst_xattrs("user.a", "1", NULL, NULL);
		err = rt_set_dxattr(d.rtd_os, obj, nvl);
		fnvlist_free(nvl);
	}
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0) {
		nvl = hyst_xattrs("user.a", "2", NULL, NULL);
		err = rt_set_dxattr(d.rtd_os, obj, nvl);
		fnvlist_free(nvl);
	}
	rt_close(&d);
	RT_CHECK(err, "change xattr");

	if (hyst_finish(1, 0, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H23: xattr present on one side only: a real edit.
 */
static int
test_hysteria_xattr_added(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("H23: xattr added on one side only");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0) {
		nvl = hyst_xattrs("user.a", "1", NULL, NULL);
		err = rt_set_dxattr(d.rtd_os, obj, nvl);
		fnvlist_free(nvl);
	}
	rt_close(&d);
	RT_CHECK(err, "add xattr");

	if (hyst_finish(1, 0, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H24: representation flip -- SA form in base, hidden-directory
 * form with the same logical set on the left. Hysterical: the
 * engine compares logically, never by form.
 */
static int
test_hysteria_xattr_repr_flip(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("H24: xattr representation flip, same set");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, &obj);
	if (err == 0) {
		nvl = hyst_xattrs("user.a", "v", NULL, NULL);
		err = rt_set_dxattr(d.rtd_os, obj, nvl);
		fnvlist_free(nvl);
	}
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0)
		err = rt_remove_sa_attr(d.rtd_os, obj, ZPL_DXATTR);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, obj, "user.a",
		    "v", 1);
	rt_close(&d);
	RT_CHECK(err, "flip representation");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H25: hidden-directory xattrs equal on both sides (the shared,
 * cloned xattr dir); the file is touched so the pair escapes the
 * fast path and the dir-form read path actually runs.
 */
static int
test_hysteria_xattr_dir_equal(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H25: dir-form xattrs equal both sides");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, &obj);
	if (err == 0)
		err = rt_add_xattr_dir_entry(d.rtd_os, obj, "user.a",
		    "v", 1);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0)
		err = rt_touch(d.rtd_os, obj);
	rt_close(&d);
	RT_CHECK(err, "touch");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H26: symlink recreated with the same target (new dnode, same
 * ZPL_SYMLINK, same size): hysterical. Also lands the
 * rt_create_symlink helper that unblocks W19.
 */
static int
test_hysteria_symlink_same(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H26: symlink recreated, same target");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "lnk",
	    "some/target", NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "lnk");
	if (err == 0)
		err = rt_create_symlink(d.rtd_os, d.rtd_root, "lnk",
		    "some/target", NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate symlink");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H27: symlink target changed, same length so ZPL_SIZE cannot
 * catch it -- the ZPL_SYMLINK compare must.
 */
static int
test_hysteria_symlink_differs(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H27: symlink target changed");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_symlink(d.rtd_os, d.rtd_root, "lnk",
	    "target-aaaa", NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "lnk");
	if (err == 0)
		err = rt_create_symlink(d.rtd_os, d.rtd_root, "lnk",
		    "target-bbbb", NULL);
	rt_close(&d);
	RT_CHECK(err, "retarget symlink");

	if (hyst_finish(1, 0, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H28: device node recreated with the same rdev: hysterical. Also
 * lands the rt_create_device helper that unblocks W19.
 */
static int
test_hysteria_device_same(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H28: device recreated, same rdev");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_device(d.rtd_os, d.rtd_root, "dev", 0x1234,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "dev");
	if (err == 0)
		err = rt_create_device(d.rtd_os, d.rtd_root, "dev",
		    0x1234, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate device");

	if (hyst_finish(1, 1, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H29: device rdev changed: a real edit (ZPL_RDEV is identity).
 */
static int
test_hysteria_device_differs(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H29: device rdev changed");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_device(d.rtd_os, d.rtd_root, "dev", 0x1234,
	    NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "dev");
	if (err == 0)
		err = rt_create_device(d.rtd_os, d.rtd_root, "dev",
		    0x9999, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate device");

	if (hyst_finish(1, 0, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H30: file replaced by a directory at the same path: never
 * hysterical (dir vs non-dir is checked before any tier).
 */
static int
test_hysteria_type_flip(void)
{
	rt_ds_t d;
	int err;

	TEST_START("H30: file replaced by dir at same path");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "x", "x", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "x");
	if (err == 0)
		err = rt_create_dir(d.rtd_os, d.rtd_root, "x", NULL);
	rt_close(&d);
	RT_CHECK(err, "type flip");

	if (hyst_finish(1, 0, 1, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H31: axis independence (retrospective-2 bug 2 regression at
 * counter level). Left removes one of two hardlinks: the survivor
 * path's CONTENT is hysterical (nlink bookkeeping is not
 * identity), while the linkpool counters still see both paths as
 * members on base and right. hl=1 with lk=2 is the signature that
 * content hysteria did not erase linkpool visibility.
 */
static int
test_hysteria_linkpool_axis(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H31: hysteria never masks the linkpool axis");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "A", "shared\n", 7,
	    &obj);
	if (err == 0)
		err = rt_add_hardlink(d.rtd_os, d.rtd_root, "B", obj);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_remove_entry(d.rtd_os, d.rtd_root, "B");
	rt_close(&d);
	RT_CHECK(err, "unlink B");

	if (hyst_finish(2, 1, 2, 2))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * H33: ZPL_GEN stripped from a compared object -- the engine's
 * hard-EIO stance on attributes a ZPL >= 5 dataset guarantees.
 * Plain SET_ERROR, safe on debug and production libzpool alike.
 */
static int
test_hysteria_gen_missing(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("H33: missing ZPL_GEN is a hard EIO");
	RT_CHECK(rt_scaffold_empty_base(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_SRC, &d), "hold src");
	err = rt_create_file(d.rtd_os, d.rtd_root, "f", "x", 1, NULL);
	rt_close(&d);
	RT_CHECK(err, "populate src");
	RT_CHECK(rt_scaffold_snap_and_clone(), "snap+clone");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "f", &obj);
	if (err == 0)
		err = rt_remove_sa_attr(d.rtd_os, obj, ZPL_GEN);
	rt_close(&d);
	RT_CHECK(err, "strip gen");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == EIO, "expected EIO");
	TEST_PASS();
}

/*
 * H36: an unparseable ZPL_DXATTR blob is corrupt on-disk input and
 * must surface as EIO, never as the unpacker's EINVAL (which reads
 * as "bad arguments" at the ioctl boundary). Found 2026-08-23 by
 * the membership-merge fault-cell plotting; fixed into
 * hysterical-detect. The garbage write dirties the dnode, so the
 * content tiers run: gen holds, identity holds, and the xattr
 * stage hits the corrupt blob.
 */
static int
test_hysteria_dxattr_corrupt(void)
{
	static const uchar_t garbage[] = {
		0xde, 0xad, 0xbe, 0xef, 0xba, 0xdd, 0xca, 0xfe
	};
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("H36: corrupt DXATTR blob is EIO, not EINVAL");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_set_sa_blob(d.rtd_os, obj, ZPL_DXATTR,
		    garbage, sizeof (garbage));
	rt_close(&d);
	RT_CHECK(err, "write garbage blob");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == EIO, "expected EIO");
	TEST_PASS();
}

/*
 * H35: side symmetry -- H11's fixture (same-length different
 * rewrite) built on the RIGHT. Only the right counter may move.
 */
static int
test_hysteria_right_side(void)
{
	rt_ds_t d;
	uint64_t obj;
	int err;

	TEST_START("H35: right-side counter symmetry");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj);
	if (err == 0)
		err = rt_edit_file(d.rtd_os, obj, "WORLD\n", 6);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	if (hyst_finish(3, 3, 2, 0))
		TEST_FAIL("stats mismatch");
	TEST_PASS();
}

/*
 * ================================================================
 * Crossref-era suppression tests (sprint-1 vintage). These assert
 * manifest behavior that only exists once cross-reference lands;
 * until then they fail cleanly through the defensive accessors.
 * They will be re-plotted into the crossref matrix.
 * ================================================================
 */

/*
 * Both sides edit the same file to identical content: suppressed.
 */
static int
test_suppress_both_edit_identical(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("suppress: both EDIT identical content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "same-fix\n", 9);
	rt_close(&d);
	RT_CHECK(err, "edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	err = rt_edit_file(d.rtd_os, obj, "same-fix\n", 9);
	rt_close(&d);
	RT_CHECK(err, "edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical edits)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides add the same path with identical content: suppressed.
 */
static int
test_suppress_both_add_identical(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("suppress: both ADD identical content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_create_file(d.rtd_os, d.rtd_root, "shared",
	    "identical\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_create_file(d.rtd_os, d.rtd_root, "shared",
	    "identical\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "create right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical adds)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides move the same file to the same destination and make
 * identical edits: suppressed.
 */
static int
test_suppress_both_move_edit_identical(void)
{
	rt_ds_t d;
	uint64_t obj;
	nvlist_t *nvl;
	int err;

	TEST_START("suppress: both MOVE_EDIT identical");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_edit_file(d.rtd_os, obj, "moved-and-fixed\n", 16);
	rt_close(&d);
	RT_CHECK(err, "move+edit left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	VERIFY0(rt_dir_lookup(d.rtd_os, d.rtd_root, "hello", &obj));
	VERIFY0(rt_rename_file(d.rtd_os, d.rtd_root, "hello",
	    d.rtd_root, "hello2"));
	err = rt_edit_file(d.rtd_os, obj, "moved-and-fixed\n", 16);
	rt_close(&d);
	RT_CHECK(err, "move+edit right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical move+edit)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides rename-on-save with content unchanged from base: the
 * walker sees new obj numbers, but the content comparison says
 * nothing happened. No change recorded at all.
 */
static int
test_edge_hysterical_both_same_content(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both hysterical, same base content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "world\n", 6, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (hysterical no-op)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides rename-on-save to the same NEW content: each side is
 * a real edit vs base, but cross-reference suppresses the pair as
 * identical.
 */
static int
test_edge_hysterical_both_identical_new(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both hysterical, identical new content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "new-content\n", 12, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "new-content\n", 12, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 0,
	    "expected 0 conflicts (identical hysterical edits)");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Both sides rename-on-save to DIFFERENT new content: a real
 * both-modified conflict through the COW boundary.
 */
static int
test_edge_hysterical_both_different(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: both hysterical, different new content");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "left-new\n", 9, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "right-new\n", 10, NULL);
	rt_close(&d);
	RT_CHECK(err, "hysterical right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello"),
	    "expected BOTH_MODIFIED at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

/*
 * Delete-and-recreate on both sides with different content: same
 * mechanism as rename-on-save (new obj under the old path), and the
 * differing content makes it a both-modified conflict.
 */
static int
test_edge_delete_and_recreate(void)
{
	rt_ds_t d;
	nvlist_t *nvl;
	int err;

	TEST_START("edge: delete + recreate both sides (diff)");
	RT_CHECK(rt_scaffold_basic(), "scaffold failed");

	RT_CHECK(rt_open(RT_DS_LEFT, &d), "hold left");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "left-recreated\n", 15, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate left");

	RT_CHECK(rt_open(RT_DS_RIGHT, &d), "hold right");
	err = rt_hysterical_edit(d.rtd_os, d.rtd_root, "hello",
	    "right-recreated\n", 16, NULL);
	rt_close(&d);
	RT_CHECK(err, "recreate right");

	rt_sync_pool();
	err = rt_run_rebase(&nvl);
	rt_scaffold_teardown();
	TEST_EXPECT(err == 0, "rebase failed");
	TEST_EXPECT(rt_manifest_nconflicts(nvl) == 1,
	    "expected 1 conflict");
	TEST_EXPECT(rt_manifest_has_conflict(nvl, "BOTH_MODIFIED",
	    "hello"),
	    "expected BOTH_MODIFIED at hello");
	fnvlist_free(nvl);
	TEST_PASS();
}

void
run_hysteria_tests(void)
{
	(void) printf("\n[hysteria: H matrix, dbgmsg counter "
	    "assertions]\n");
	(void) test_hysteria_untouched_fast_path();
	(void) test_hysteria_neighbor_churn();
	(void) test_hysteria_touch_only();
	(void) test_hysteria_sa_identity();
	(void) test_hysteria_acl();
	(void) test_hysteria_recycled_gen();
	(void) test_hysteria_rename_on_save();
	(void) test_hysteria_recreate_differs();
	(void) test_hysteria_rewrite_identical();
	(void) test_hysteria_rewrite_differs();
	(void) test_hysteria_empty_files();
	(void) test_hysteria_append();
	(void) test_hysteria_multiblock();
	(void) test_hysteria_hole_vs_zeros();
	(void) test_hysteria_dir_untouched();
	(void) test_hysteria_dir_entries();
	(void) test_hysteria_dir_chmod();
	(void) test_hysteria_xattr_sa_equal();
	(void) test_hysteria_xattr_value();
	(void) test_hysteria_xattr_added();
	(void) test_hysteria_xattr_repr_flip();
	(void) test_hysteria_xattr_dir_equal();
	(void) test_hysteria_symlink_same();
	(void) test_hysteria_symlink_differs();
	(void) test_hysteria_device_same();
	(void) test_hysteria_device_differs();
	(void) test_hysteria_type_flip();
	(void) test_hysteria_linkpool_axis();
	(void) test_hysteria_gen_missing();
	(void) test_hysteria_dxattr_corrupt();
	(void) test_hysteria_right_side();

	(void) printf("\n[hysteria: crossref-era suppression, "
	    "fail-clean until crossref lands]\n");
	(void) test_suppress_both_edit_identical();
	(void) test_suppress_both_add_identical();
	(void) test_suppress_both_move_edit_identical();
	(void) test_edge_hysterical_both_same_content();
	(void) test_edge_hysterical_both_identical_new();
	(void) test_edge_hysterical_both_different();
	(void) test_edge_delete_and_recreate();
}
