/*
 * Stub rebase_test.h for syntax-checking harness sources on a
 * machine with no ZFS headers. Driven by devcheck/syncheck.sh,
 * which copies this file (as rebase_test.h) plus the sources into
 * a temp dir so the quote-include resolves here instead of the
 * real header.
 *
 * MAINTENANCE: keep in sync with the real rebase_test.h -- every
 * rt_* prototype and TEST macro lives in both, and every libzpool
 * API a harness file calls needs a matching fake here (match the
 * real signature; arity and pointer shape are what this checks).
 * Signatures come from the FREEBSD BOX's /usr/src tree, which is
 * what the harness actually compiles against -- NOT the local zfs
 * research checkout when the two vintages drift (spa_scan grew a
 * flags arg upstream that the box does not have; that skew broke
 * a build once). Shapes are fakes; the FreeBSD build remains the
 * authority.
 */
#ifndef	_REBASE_TEST_H
#define	_REBASE_TEST_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

typedef enum { B_FALSE, B_TRUE } boolean_t;
typedef unsigned int uint_t;
typedef unsigned char uchar_t;
typedef struct objset { struct { uint16_t *sa_user_table; } *os_sa; } objset_t;
typedef struct nvlist nvlist_t;
typedef struct nvpair nvpair_t;
typedef struct dmu_tx dmu_tx_t;
typedef struct sa_handle sa_handle_t;
typedef uint16_t sa_attr_type_t;
typedef struct sa_bulk_attr { void *p[4]; int i[2]; } sa_bulk_attr_t;
typedef struct cred cred_t;
typedef struct dmu_object_info { int doi_type; } dmu_object_info_t;
typedef struct spa spa_t;
typedef struct dsl_pool dsl_pool_t;

#define	POOL_NAME	"rtest"
#define	VDEV_SIZE	(128ULL << 20)
#define	VDEV_PATH	"/tmp/rtest_vdev"
#define	RT_DS_SRC	POOL_NAME "/src"
#define	RT_DS_LEFT	POOL_NAME "/left"
#define	RT_DS_RIGHT	POOL_NAME "/right"
#define	ZFS_DIRENT_MAKE(type, obj) ((uint64_t)(obj) | \
	    ((uint64_t)(type) << 60))
#define	ZFS_DIRENT_OBJ(x) ((x) & 0xffffffffffffULL)

enum { ZPL_ATIME, ZPL_MTIME, ZPL_CTIME, ZPL_CRTIME, ZPL_GEN,
	ZPL_MODE, ZPL_SIZE, ZPL_PARENT, ZPL_LINKS, ZPL_XATTR,
	ZPL_RDEV, ZPL_FLAGS, ZPL_UID, ZPL_GID, ZPL_PAD,
	ZPL_ZNODE_ACL, ZPL_DACL_COUNT, ZPL_SYMLINK, ZPL_SCANSTAMP,
	ZPL_DACL_ACES, ZPL_DXATTR, ZPL_PROJID, ZPL_END };

#define	MASTER_NODE_OBJ 1
#define	ZFS_SA_ATTRS "SA_ATTRS"
#define	ZFS_ROOT_OBJ "ROOT"
#define	ZFS_UNLINKED_SET "DELETE_QUEUE"
#define	ZPL_VERSION_STR "VERSION"
#define	ZPL_VERSION 5
#define	DMU_OT_MASTER_NODE 21
#define	DMU_OT_NONE 0
#define	DMU_OT_SA_MASTER_NODE 32
#define	DMU_OT_UNLINKED_SET 22
#define	DMU_OT_DIRECTORY_CONTENTS 20
#define	DMU_OT_PLAIN_FILE_CONTENTS 19
#define	DMU_OT_SA 34
#define	DMU_NEW_OBJECT (-1ULL)
#define	DNODE_MIN_SIZE 512
#define	DN_BONUS_SIZE(x) ((x) - 64)
#define	SA_HDL_SHARED 0
#define	SA_HDL_PRIVATE 1
#define	DMU_TX_WAIT 1
#define	NV_ENCODE_XDR 1
#define	KM_SLEEP 0
#define	FTAG ((void *)(uintptr_t)__func__)
#define	VERIFY0(x) do { if ((x) != 0) abort(); } while (0)
#define	SA_ADD_BULK_ATTR(a, c, attr, cb, data, len) \
	do { (void)(attr); (void)(data); (void)(len); (c)++; } while (0)

extern int zfs_dbgmsg_enable;
void zfs_dbgmsg_print(int fd, const char *tag);

int dsl_rebase(const char *, const char *, nvlist_t *);

nvlist_t *fnvlist_alloc(void);
void fnvlist_free(nvlist_t *);
void nvlist_free(nvlist_t *);
void fnvlist_add_string(nvlist_t *, const char *, const char *);
void fnvlist_add_uint64(nvlist_t *, const char *, uint64_t);
void fnvlist_add_nvlist_array(nvlist_t *, const char *,
    const nvlist_t **, uint_t);
void fnvlist_add_byte_array(nvlist_t *, const char *, const uchar_t *,
    uint_t);
int nvlist_size(nvlist_t *, size_t *, int);
int nvlist_pack(nvlist_t *, char **, size_t *, int, int);
int nvlist_lookup_uint64(nvlist_t *, const char *, uint64_t *);
int nvlist_lookup_nvlist_array(nvlist_t *, const char *, nvlist_t ***,
    uint_t *);
const char *fnvlist_lookup_string(nvlist_t *, const char *);

int dmu_objset_own(const char *, int, boolean_t, boolean_t, void *,
    objset_t **);
void dmu_objset_disown(objset_t *, boolean_t, void *);
int dmu_objset_create(const char *, int, uint64_t, void *,
    void (*)(objset_t *, void *, cred_t *, dmu_tx_t *), void *);
int dmu_objset_snapshot_one(const char *, const char *);
int dsl_dataset_clone(const char *, const char *);
int spa_create(const char *, nvlist_t *, nvlist_t *, void *, void *);
int spa_destroy(const char *);
int spa_open(const char *, spa_t **, const void *);
void spa_close(spa_t *, const void *);
dsl_pool_t *spa_get_dsl(spa_t *);
void txg_wait_synced(dsl_pool_t *, uint64_t);
typedef int pool_scan_func_t;
typedef int pool_scrub_cmd_t;
typedef int pool_scrub_flags_t;
#define	POOL_SCAN_SCRUB 1
#define	POOL_SCRUB_PAUSE 1
int spa_scan(spa_t *, pool_scan_func_t);
int dsl_scrub_set_pause_resume(const dsl_pool_t *, pool_scrub_cmd_t);
#define	ZFS_FUID_TABLES "FUID"
#define	ZPL_VERSION_SA 5
#define	DMU_OST_ZVOL 3
#define	DMU_OST_ZFS 2
#define	SPA_MODE_READ 1
#define	SPA_MODE_WRITE 2
enum { ZFS_CASE_SENSITIVE, ZFS_CASE_INSENSITIVE, ZFS_CASE_MIXED };
#define	ZPOOL_CONFIG_TYPE "type"
#define	ZPOOL_CONFIG_PATH "path"
#define	ZPOOL_CONFIG_ASHIFT "ashift"
#define	ZPOOL_CONFIG_CHILDREN "children"
#define	VDEV_TYPE_FILE "file"
#define	VDEV_TYPE_ROOT "root"

dmu_tx_t *dmu_tx_create(objset_t *);
void dmu_tx_hold_zap(dmu_tx_t *, uint64_t, boolean_t, const char *);
void dmu_tx_hold_sa_create(dmu_tx_t *, int);
void dmu_tx_hold_sa(dmu_tx_t *, sa_handle_t *, boolean_t);
void dmu_tx_hold_write(dmu_tx_t *, uint64_t, uint64_t, uint64_t);
void dmu_tx_hold_bonus(dmu_tx_t *, uint64_t);
int dmu_tx_assign(dmu_tx_t *, int);
void dmu_tx_abort(dmu_tx_t *);
void dmu_tx_commit(dmu_tx_t *);
uint64_t dmu_tx_get_txg(dmu_tx_t *);
uint64_t dmu_object_alloc(objset_t *, int, int, int, int, dmu_tx_t *);
int dmu_object_info(objset_t *, uint64_t, dmu_object_info_t *);
void dmu_write(objset_t *, uint64_t, uint64_t, uint64_t, const void *,
    dmu_tx_t *, int);

int zap_create_claim(objset_t *, uint64_t, int, int, int, dmu_tx_t *);
uint64_t zap_create(objset_t *, int, int, int, dmu_tx_t *);
uint64_t zap_create_norm(objset_t *, int, int, int, int, dmu_tx_t *);
int zap_add(objset_t *, uint64_t, const char *, int, int, const void *,
    dmu_tx_t *);
int zap_update(objset_t *, uint64_t, const char *, int, int,
    const void *, dmu_tx_t *);
int zap_lookup(objset_t *, uint64_t, const char *, int, int, void *);
int zap_remove(objset_t *, uint64_t, const char *, dmu_tx_t *);

int sa_setup(objset_t *, uint64_t, const void *, int,
    sa_attr_type_t **);
int sa_handle_get(objset_t *, uint64_t, void *, int, sa_handle_t **);
void sa_handle_destroy(sa_handle_t *);
int sa_lookup(sa_handle_t *, sa_attr_type_t, void *, uint32_t);
int sa_update(sa_handle_t *, sa_attr_type_t, void *, uint32_t,
    dmu_tx_t *);
int sa_remove(sa_handle_t *, sa_attr_type_t, dmu_tx_t *);
int sa_size(sa_handle_t *, sa_attr_type_t, int *);
int sa_replace_all_by_template(sa_handle_t *, sa_bulk_attr_t *, int,
    dmu_tx_t *);
extern const int zfs_attr_table[];

/* pass/fail counters */
extern int rt_tests_run;
extern int rt_tests_passed;
extern int rt_tests_failed;

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

#define	RT_CHECK(err, msg) do {						\
	if ((err) != 0) {						\
		rt_scaffold_teardown();					\
		TEST_FAIL(msg);						\
	}								\
} while (0)

typedef struct rt_ds {
	objset_t	*rtd_os;
	uint64_t	rtd_root;
} rt_ds_t;

typedef struct rt_walk_stats {
	uint64_t	rws_visited;
	uint64_t	rws_hyst_left;
	uint64_t	rws_hyst_right;
	uint64_t	rws_linked;
} rt_walk_stats_t;

typedef struct rt_move_stats {
	uint64_t	rms_moves_left;
	uint64_t	rms_moves_right;
	uint64_t	rms_move_edits_left;
	uint64_t	rms_move_edits_right;
} rt_move_stats_t;

typedef struct rt_anchor_stats {
	uint64_t	ras_left[4];
	uint64_t	ras_right[4];
} rt_anchor_stats_t;

typedef struct rt_target_stats {
	uint64_t	rts_left[6];
	uint64_t	rts_right[6];
} rt_target_stats_t;

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

int rt_open(const char *dsname, rt_ds_t *ds);
void rt_close(rt_ds_t *ds);
void rt_sync_pool(void);
int rt_run_rebase(nvlist_t **nvlp);
int rt_walk_stats(rt_walk_stats_t *ws);
int rt_changelist_counts(uint64_t *leftp, uint64_t *rightp);
int rt_move_stats(rt_move_stats_t *ms);
int rt_anchor_stats(rt_anchor_stats_t *as);
int rt_target_stats(rt_target_stats_t *ts);
int rt_final_stats(rt_final_stats_t *fs);
int rt_conflict_stats(rt_conflict_stats_t *cs);
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

int rt_scaffold_basic(void);
int rt_scaffold_empty_base(void);
int rt_scaffold_snap_and_clone(void);
void rt_scaffold_teardown(void);
int rt_snapshot(const char *dsname, const char *snapname);
int rt_clone(const char *clone_name, const char *snap_name);
int rt_create_zpl_dataset(const char *dsname);
int rt_create_zvol_dataset(const char *dsname);

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

void run_basic_tests(void);
void run_setup_tests(void);
void run_walk_tests(void);
void run_hysteria_tests(void);
void run_diff_tests(void);
void run_moves_tests(void);
void run_anchor_tests(void);
void run_merge_tests(void);
void run_emit_tests(void);
void run_linkpool_tests(void);
void run_crossref_tests(void);

#endif	/* _REBASE_TEST_H */
#ifndef IFTODT
#define IFTODT(m) (((m) & 0170000) >> 12)
#endif
