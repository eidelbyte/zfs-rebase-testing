// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Shared declarations for the zfs rebase test harness.
 *
 * The harness links against libzpool to exercise the kernel rebase
 * code in userspace: it creates pools on file vdevs, populates
 * ZPL-compatible datasets via raw DMU/ZAP/SA calls, takes snapshots
 * and clones, and calls dsl_rebase().
 *
 * File map:
 *   rebase_test_main.c  main(), section table, result summary
 *   rt_harness.c        counters, dataset handles, rebase runner,
 *                       manifest inspection
 *   rt_scaffold.c       pool/vdev lifecycle, scaffolds, snapshots
 *   rt_zpl.c            raw DMU/ZAP/SA object manipulation
 *   test_basic.c        walk + standalone diff + error cases
 *   test_hysteria.c     identical-content suppression
 *   test_diff.c         standalone-diff matrix (two-axis records)
 *   test_moves.c        rename detection and move conflicts
 *   test_linkpool.c     hardlink cases (sprint-2 catalog lands here)
 *   test_crossref.c     conflict detection and clean merges
 *
 * Helper prefix rt_ = "rebase test". Helpers return 0 or an errno.
 * Test functions return 0 on pass and nonzero on fail, and are
 * called from their file's run_*_tests() section runner.
 */

#ifndef	_REBASE_TEST_H
#define	_REBASE_TEST_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
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

#define	RT_DS_SRC	POOL_NAME "/src"
#define	RT_DS_LEFT	POOL_NAME "/left"
#define	RT_DS_RIGHT	POOL_NAME "/right"

#define	ZFS_DIRENT_MAKE(type, obj)	((uint64_t)(obj) | \
	    ((uint64_t)(type) << 60))

/* pass/fail counters, defined in rt_harness.c, read by main() */
extern int rt_tests_run;
extern int rt_tests_passed;
extern int rt_tests_failed;

/*
 * For scaffold-style helpers that return an errno: report and
 * propagate on failure.
 */
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
	rt_tests_run++;							\
} while (0)

#define	TEST_PASS() do {						\
	(void) printf("PASS\n");					\
	rt_tests_passed++;						\
	return (0);							\
} while (0)

#define	TEST_FAIL(msg) do {						\
	(void) printf("FAIL: %s\n", (msg));				\
	rt_tests_failed++;						\
	return (1);							\
} while (0)

#define	TEST_EXPECT(cond, msg) do {					\
	if (!(cond)) {							\
		TEST_FAIL(msg);						\
	}								\
} while (0)

/*
 * Fail the running test (tearing the pool down first) if err is
 * nonzero. Expands to a return: use only inside a test function,
 * and never while a dataset is held -- rt_close() first.
 */
#define	RT_CHECK(err, msg) do {						\
	if ((err) != 0) {						\
		rt_scaffold_teardown();					\
		TEST_FAIL(msg);						\
	}								\
} while (0)

/*
 * An owned dataset handle: objset plus its root directory object.
 * The struct address doubles as the ownership tag, so open and
 * close must receive the same rt_ds_t.
 * Member prefix rtd_ = "rebase test dataset".
 */
typedef struct rt_ds {
	objset_t	*rtd_os;
	uint64_t	rtd_root;	/* root dir obj (MASTER_NODE) */
} rt_ds_t;

/*
 * Walk-summary counters scraped from the engine's dbgmsg line, the
 * only externally visible classification until standalone-diff
 * emits changelists. See the hysteria (H) matrix preamble.
 * Member prefix rws_ = "rebase walk stats".
 */
typedef struct rt_walk_stats {
	uint64_t	rws_visited;
	uint64_t	rws_hyst_left;
	uint64_t	rws_hyst_right;
	uint64_t	rws_linked;
} rt_walk_stats_t;

/*
 * Move-collapse counters scraped from the engine's third
 * walk-summary dbgmsg line. See the move-collapse (M) matrix
 * preamble. Member prefix rms_ = "rebase move stats".
 */
typedef struct rt_move_stats {
	uint64_t	rms_moves_left;
	uint64_t	rms_moves_right;
	uint64_t	rms_move_edits_left;
	uint64_t	rms_move_edits_right;
} rt_move_stats_t;

/*
 * Cross-reference phase A/B counters scraped from the engine's
 * per-branch summary lines. Array order matches the dbgmsg text:
 * anchors are (anchored, novel, recycled, fragment); targets are
 * (same, gone, standalone, anchor, fragment, novel). See the
 * linkpool-anchor (A/T) matrix preamble.
 */
typedef struct rt_anchor_stats {
	uint64_t	ras_left[4];
	uint64_t	ras_right[4];
} rt_anchor_stats_t;

typedef struct rt_target_stats {
	uint64_t	rts_left[6];
	uint64_t	rts_right[6];
} rt_target_stats_t;

/*
 * Cross-reference phase C/D counters scraped from the engine's
 * merge summary lines. Finals order matches the dbgmsg text (same,
 * gone, standalone, anchor, fragment, novel, conflict -- the last
 * counts rows left undecided). See the membership-merge (U/R)
 * matrix preamble.
 */
typedef struct rt_final_stats {
	uint64_t	rfs_kind[7];
} rt_final_stats_t;

typedef struct rt_conflict_stats {
	uint64_t	rcs_total;
	uint64_t	rcs_relink;
	uint64_t	rcs_divergent;
	uint64_t	rcs_overlap;
	uint64_t	rcs_content;
} rt_conflict_stats_t;

/* rt_harness.c */
int rt_open(const char *dsname, rt_ds_t *ds);
void rt_close(rt_ds_t *ds);
void rt_sync_pool(void);
int rt_run_rebase(nvlist_t **nvlp);
void rt_rebase_diag(void);
int rt_walk_stats(rt_walk_stats_t *ws);
int rt_changelist_counts(uint64_t *leftp, uint64_t *rightp);
int rt_move_stats(rt_move_stats_t *ms);
int rt_anchor_stats(rt_anchor_stats_t *as);
int rt_target_stats(rt_target_stats_t *ts);
int rt_final_stats(rt_final_stats_t *fs);
int rt_conflict_stats(rt_conflict_stats_t *cs);

/*
 * test_diff.c -- the shared ten-tuple finisher (D six-tuple plus
 * the four move-collapse counters); test_moves.c aliases it as
 * moves_finish. Runs the rebase, scrapes all three summary lines,
 * tears the scaffold down, compares, and reports any mismatch.
 */
int diff_finish_full(uint64_t ev, uint64_t ehl, uint64_t ehr,
    uint64_t elk, uint64_t ecl, uint64_t ecr, uint64_t eml,
    uint64_t emr, uint64_t emel, uint64_t emer);
uint64_t rt_manifest_nconflicts(nvlist_t *nvl);
uint64_t rt_manifest_left_nchanges(nvlist_t *nvl);
uint64_t rt_manifest_right_nchanges(nvlist_t *nvl);
boolean_t rt_manifest_has_conflict(nvlist_t *nvl, const char *type,
    const char *path);
uint64_t rt_manifest_conflict_nalt(nvlist_t *nvl, const char *path);
uint64_t rt_manifest_nwarnings(nvlist_t *nvl);
uint64_t rt_manifest_nactions(nvlist_t *nvl);
boolean_t rt_manifest_has_warning(nvlist_t *nvl, const char *kind,
    const char *path);
void rt_manifest_dump(nvlist_t *nvl);

/* rt_scaffold.c */
int rt_scaffold_basic(void);
int rt_scaffold_empty_base(void);
int rt_scaffold_snap_and_clone(void);
void rt_scaffold_teardown(void);
int rt_snapshot(const char *dsname, const char *snapname);
int rt_clone(const char *clone_name, const char *snap_name);
int rt_create_zpl_dataset(const char *dsname);
int rt_create_zvol_dataset(const char *dsname);

/* rt_zpl.c */
void rt_zpl_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
int rt_sa_setup(objset_t *os, sa_attr_type_t **tblp);
int rt_dir_lookup(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t *objp);
int rt_create_file(objset_t *os, uint64_t dir_obj, const char *name,
    const void *data, uint64_t datalen, uint64_t *objp);
int rt_create_dir(objset_t *os, uint64_t parent_obj, const char *name,
    uint64_t *objp);
int rt_remove_entry(objset_t *os, uint64_t dir_obj, const char *name);
int rt_edit_file(objset_t *os, uint64_t obj, const void *data,
    uint64_t datalen);
int rt_add_hardlink(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t target_obj);
int rt_hysterical_edit(objset_t *os, uint64_t dir_obj, const char *name,
    const void *data, uint64_t datalen, uint64_t *new_objp);
int rt_rename_file(objset_t *os, uint64_t src_dir, const char *old_name,
    uint64_t dst_dir, const char *new_name);
int rt_set_nlink(objset_t *os, uint64_t obj, uint64_t nlink);
int rt_set_zplprop(objset_t *os, const char *name, uint64_t value);
int rt_add_dangling_entry(objset_t *os, uint64_t dir_obj,
    const char *name);
int rt_set_sa_u64(objset_t *os, uint64_t obj, int zpl_attr,
    uint64_t value);
int rt_set_sa_blob(objset_t *os, uint64_t obj, int zpl_attr,
    const void *buf, uint32_t len);
int rt_remove_sa_attr(objset_t *os, uint64_t obj, int zpl_attr);
int rt_touch(objset_t *os, uint64_t obj);
int rt_write_range(objset_t *os, uint64_t obj, uint64_t offset,
    const void *data, uint64_t len, uint64_t newsize);
int rt_set_dxattr(objset_t *os, uint64_t obj, nvlist_t *xattrs);
int rt_add_xattr_dir_entry(objset_t *os, uint64_t file_obj,
    const char *name, const void *value, uint64_t vlen);
int rt_create_symlink(objset_t *os, uint64_t dir_obj, const char *name,
    const char *target, uint64_t *objp);
int rt_create_device(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t rdev, uint64_t *objp);

/* section runners */
void run_basic_tests(void);
void run_setup_tests(void);
void run_walk_tests(void);
void run_hysteria_tests(void);
void run_diff_tests(void);
void run_moves_tests(void);
void run_anchor_tests(void);
void run_merge_tests(void);
void run_emit_tests(void);
void run_seam_tests(void);
void run_linkpool_tests(void);
void run_crossref_tests(void);

#endif	/* _REBASE_TEST_H */
