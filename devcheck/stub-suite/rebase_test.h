/*
 * Stub rebase_test.h for syntax-checking the .tree SUITE sources on a
 * machine with no ZFS headers.  Driven by devcheck/suitecheck.sh.
 *
 * Separate from devcheck/stub/rebase_test.h on purpose.  That stub
 * fakes the rebase types along with everything else, which is right
 * for the revision-2 battery; the suite is written against the REAL
 * revision-3 contract -- rebase_decision_t and everything it points
 * at -- so faking those would defeat the check.  Here the real
 * sys/dsl_rebase.h is included through the fake sys/ tree in
 * ../scripts/header-stub, exactly as scripts/engine-syncheck.sh does
 * for engine code, and only the harness API below is faked.
 *
 * MAINTENANCE: this declares the rt_* helpers the SUITE calls, not
 * every helper the harness has.  When a suite source starts calling a
 * new one, add it here with the signature from the real
 * rebase_test.h -- arity and pointer shape are what this verifies.
 * The FreeBSD build remains the authority.
 */
#ifndef	_REBASE_TEST_H
#define	_REBASE_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

/* The real contract, through the fake sys/ tree. */
#include <sys/dsl_rebase.h>

#define	POOL_NAME	"rtest"
#define	RT_DS_SRC	POOL_NAME "/src"
#define	RT_DS_LEFT	POOL_NAME "/left"
#define	RT_DS_RIGHT	POOL_NAME "/right"

#define	SPA_MODE_READ	1
#define	SPA_MODE_WRITE	2

/* ZPL SA attribute ids; only the one the materializer repairs. */
enum { ZPL_ATIME, ZPL_MTIME, ZPL_CTIME, ZPL_CRTIME, ZPL_GEN,
	ZPL_MODE, ZPL_SIZE, ZPL_PARENT, ZPL_LINKS, ZPL_END };

typedef struct rt_ds {
	objset_t	*rtd_os;
	uint64_t	rtd_root;
} rt_ds_t;

/* rt_harness.c */
int rt_open(const char *dsname, rt_ds_t *ds);
void rt_close(rt_ds_t *ds);
void rt_sync_pool(void);
int rt_dbgmsg_last(const char *needle, char *line_out, size_t outlen);

/* rt_scaffold.c */
int rt_scaffold_empty_base(void);
int rt_scaffold_snap_and_clone(void);
void rt_scaffold_teardown(void);
int rt_snapshot(const char *dsname, const char *snapname);

/* rt_zpl.c */
int rt_create_file(objset_t *os, uint64_t dir_obj, const char *name,
    const void *data, uint64_t datalen, uint64_t *objp);
int rt_create_dir(objset_t *os, uint64_t parent_obj, const char *name,
    uint64_t *objp);
int rt_remove_entry(objset_t *os, uint64_t dir_obj, const char *name);
int rt_edit_file(objset_t *os, uint64_t obj, const void *data,
    uint64_t datalen);
int rt_add_hardlink(objset_t *os, uint64_t dir_obj, const char *name,
    uint64_t target_obj);
int rt_rename_file(objset_t *os, uint64_t src_dir, const char *old_name,
    uint64_t dst_dir, const char *new_name);
int rt_set_sa_u64(objset_t *os, uint64_t obj, int zpl_attr, uint64_t value);
int rt_read_data(objset_t *os, uint64_t obj, uint64_t off, uint64_t len,
    void *buf);

#endif	/* _REBASE_TEST_H */
