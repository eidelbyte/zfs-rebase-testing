// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Materialize a parsed .tree fixture onto a real pool.
 *
 * WHAT A FIXTURE KEY MEANS.  A line's "idx-txg" is not an object
 * number and never becomes one.  It is the claim "every line wearing
 * this key reaches ONE dnode".  That gives three cases, and the
 * materializer only has to honor them, not invent anything:
 *
 *   - a key in base and in a side: the side kept base's dnode, which
 *     cloning provides for free.  Nothing to do.
 *   - a key in base under DIFFERENT names on a side: the side moved
 *     it.  A rename, not a delete and a create, because a delete and
 *     a create would destroy the identity the fixture is asserting.
 *   - a key on one side only: a fresh dnode.  dmu_object_alloc picks
 *     the number and the fixture never learns it.
 *
 * So each side is built as a DELTA against base, and the delta is
 * derived rather than written down: the fixture states the two end
 * states, and the sequence of operations between them is this file's
 * problem.  That is deliberate.  A fixture that spelled out
 * operations would be asserting the harness's idea of what happened
 * instead of the scenario, and the two drift.
 *
 * Order of work, per side:
 *
 *   A. placements, shallowest first -- creates, renames and links,
 *      so that a directory exists before anything is put in it, and
 *      a directory's own move happens before its children are
 *      considered (they ride along and their paths are already
 *      right).
 *   B. removals, deepest first -- so a directory is empty by the
 *      time its own entry goes.
 *   C. content, last -- because a rename must not be mistaken for an
 *      edit, and an edit must land on whatever dnode ended up
 *      holding the name.
 *
 * A name wanted by the target but occupied by the wrong dnode is
 * moved out of the way to a scratch name at the root, and picked up
 * again when its own key's turn comes.  This is the same trick the
 * engine uses to break rotation cycles, for the same reason: without
 * it, two names that swap places cannot be built at all.
 */

#include "rt_tree_suite.h"

/*
 * ------------------------------------------------------------------
 * Key map: fixture identity to the object number it landed on
 * ------------------------------------------------------------------
 */

typedef struct kment {
	char		km_key[RT_KEYLEN];
	uint64_t	km_obj;
	int		km_isdir;
	char		*km_token;	/* what the object holds NOW */
	/*
	 * What ZPL_PARENT currently says, for directories.  Tracked
	 * rather than recomputed because a namespace rename does not
	 * touch the dnode, so this is the only record of whether the
	 * attribute still agrees with where the directory now sits --
	 * and a directory can pass through a scratch name on its way,
	 * which is two moves, not one.
	 */
	uint64_t	km_parent;
} kment_t;

typedef struct kmap {
	kment_t		*k_ents;
	int		k_n;
} kmap_t;

static kment_t *
kmap_find(kmap_t *km, const char *key)
{
	int i;

	for (i = 0; i < km->k_n; i++) {
		if (strcmp(km->k_ents[i].km_key, key) == 0)
			return (&km->k_ents[i]);
	}
	return (NULL);
}

static void
kmap_add(kmap_t *km, const char *key, uint64_t obj, int isdir,
    const char *token, uint64_t parent)
{
	kment_t *e;

	km->k_ents = rt_xrealloc(km->k_ents,
	    (size_t)(km->k_n + 1) * sizeof (kment_t));
	e = &km->k_ents[km->k_n++];
	(void) memset(e, 0, sizeof (*e));
	(void) snprintf(e->km_key, sizeof (e->km_key), "%s", key);
	e->km_obj = obj;
	e->km_isdir = isdir;
	e->km_token = rt_xstrdup(token);
	e->km_parent = parent;
}

static void
kmap_copy(kmap_t *dst, const kmap_t *src)
{
	int i;

	(void) memset(dst, 0, sizeof (*dst));
	for (i = 0; i < src->k_n; i++) {
		kmap_add(dst, src->k_ents[i].km_key, src->k_ents[i].km_obj,
		    src->k_ents[i].km_isdir, src->k_ents[i].km_token,
		    src->k_ents[i].km_parent);
	}
}

/*
 * Rename in the namespace, and repair the directory's parent
 * attribute if the move changed it.  rt_rename_file deliberately
 * leaves the dnode alone, which is right for files -- their
 * ZPL_PARENT is already ambiguous the moment a second name exists,
 * and dirtying the dnode would spoil the engine's untouched-since-
 * fork fast path -- but wrong for a directory, whose parent
 * attribute is load-bearing.
 */
static int
rename_entry(objset_t *os, kment_t *e, uint64_t fdir, const char *fleaf,
    uint64_t tdir, const char *tleaf)
{
	int err = rt_rename_file(os, fdir, fleaf, tdir, tleaf);

	if (err != 0)
		return (err);
	if (e != NULL && e->km_isdir && e->km_parent != tdir) {
		err = rt_set_sa_u64(os, e->km_obj, ZPL_PARENT, tdir);
		if (err != 0)
			return (err);
		e->km_parent = tdir;
	}
	return (0);
}

static void
kmap_free(kmap_t *km)
{
	int i;

	for (i = 0; i < km->k_n; i++)
		free(km->k_ents[i].km_token);
	free(km->k_ents);
	(void) memset(km, 0, sizeof (*km));
}

/*
 * ------------------------------------------------------------------
 * Live model: what names the dataset currently holds
 * ------------------------------------------------------------------
 */

typedef struct live_ent {
	char		*le_path;
	char		le_key[RT_KEYLEN];
} live_ent_t;

typedef struct live {
	live_ent_t	*l_ents;
	int		l_n;
} live_t;

static int
live_find(live_t *lv, const char *path)
{
	int i;

	for (i = 0; i < lv->l_n; i++) {
		if (strcmp(lv->l_ents[i].le_path, path) == 0)
			return (i);
	}
	return (-1);
}

static void
live_add(live_t *lv, const char *path, const char *key)
{
	live_ent_t *e;

	lv->l_ents = rt_xrealloc(lv->l_ents,
	    (size_t)(lv->l_n + 1) * sizeof (live_ent_t));
	e = &lv->l_ents[lv->l_n++];
	e->le_path = rt_xstrdup(path);
	(void) snprintf(e->le_key, sizeof (e->le_key), "%s", key);
}

static void
live_del(live_t *lv, int idx)
{
	free(lv->l_ents[idx].le_path);
	(void) memmove(&lv->l_ents[idx], &lv->l_ents[idx + 1],
	    (size_t)(lv->l_n - idx - 1) * sizeof (live_ent_t));
	lv->l_n--;
}

static void
live_free(live_t *lv)
{
	int i;

	for (i = 0; i < lv->l_n; i++)
		free(lv->l_ents[i].le_path);
	free(lv->l_ents);
	(void) memset(lv, 0, sizeof (*lv));
}

/*
 * Move a name, and everything underneath it.  Renaming a directory
 * in the namespace silently re-paths its whole subtree, so the live
 * model has to follow or every later lookup is wrong.
 */
static void
live_repath(live_t *lv, const char *from, const char *to)
{
	size_t flen = strlen(from);
	int i;

	for (i = 0; i < lv->l_n; i++) {
		char *old = lv->l_ents[i].le_path;
		char *new;
		size_t nlen;

		if (strcmp(old, from) == 0) {
			lv->l_ents[i].le_path = rt_xstrdup(to);
			free(old);
			continue;
		}
		if (strncmp(old, from, flen) != 0 || old[flen] != '/')
			continue;

		nlen = strlen(to) + strlen(old + flen) + 1;
		new = rt_xmalloc(nlen);
		(void) snprintf(new, nlen, "%s%s", to, old + flen);
		lv->l_ents[i].le_path = new;
		free(old);
	}
}

/*
 * ------------------------------------------------------------------
 * Walking a parsed tree in build order
 * ------------------------------------------------------------------
 */

typedef struct pathref {
	const char		*pr_path;
	const rt_tree_pool_t	*pr_pool;
} pathref_t;

/*
 * Shallowest first, then lexicographic.  Depth is what matters --
 * a parent has to exist before its child is placed -- and the tie
 * break only exists so a run is reproducible.
 */
static int
pathref_cmp(const void *a, const void *b)
{
	const pathref_t *pa = a;
	const pathref_t *pb = b;
	int da = rt_path_depth(pa->pr_path);
	int db = rt_path_depth(pb->pr_path);

	if (da != db)
		return (da < db ? -1 : 1);
	return (strcmp(pa->pr_path, pb->pr_path));
}

static pathref_t *
tree_paths(const rt_tree_t *t, int *np)
{
	pathref_t *refs = NULL;
	int n = 0;
	int i, j;

	for (i = 0; i < t->rtt_npools; i++) {
		for (j = 0; j < t->rtt_pools[i].rtp_nnames; j++) {
			refs = rt_xrealloc(refs,
			    (size_t)(n + 1) * sizeof (pathref_t));
			refs[n].pr_path = t->rtt_pools[i].rtp_names[j];
			refs[n].pr_pool = &t->rtt_pools[i];
			n++;
		}
	}
	qsort(refs, (size_t)n, sizeof (pathref_t), pathref_cmp);
	*np = n;
	return (refs);
}

/* Does the target want exactly this name on this dnode? */
static int
target_holds(const pathref_t *refs, int n, const char *path, const char *key)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strcmp(refs[i].pr_path, path) != 0)
			continue;
		return (key == NULL || strcmp(refs[i].pr_pool->rtp_key,
		    key) == 0);
	}
	return (0);
}

/*
 * ------------------------------------------------------------------
 * Directory objects
 * ------------------------------------------------------------------
 */

static int
dir_obj_of(live_t *lv, kmap_t *km, uint64_t root, const char *path,
    uint64_t *objp)
{
	int li;
	kment_t *e;

	if (strcmp(path, "/") == 0) {
		*objp = root;
		return (0);
	}
	li = live_find(lv, path);
	if (li < 0)
		return (ENOENT);
	e = kmap_find(km, lv->l_ents[li].le_key);
	if (e == NULL)
		return (ENOENT);
	*objp = e->km_obj;
	return (0);
}

/* The parent's object number for a path we are about to touch. */
static int
parent_obj_of(live_t *lv, kmap_t *km, uint64_t root, const char *path,
    uint64_t *objp)
{
	char par[1024];

	if (rt_path_parent(path, par, sizeof (par)) != 0)
		return (EINVAL);
	return (dir_obj_of(lv, km, root, par, objp));
}

#define	BUILD_ERR(rc, ...) do {						\
	(void) snprintf(errbuf, errlen, __VA_ARGS__);			\
	err = (rc);							\
	goto out;							\
} while (0)

/*
 * ------------------------------------------------------------------
 * Base
 * ------------------------------------------------------------------
 */

static int
build_base(objset_t *os, uint64_t root, const rt_tree_t *base, kmap_t *km,
    live_t *lv, char *errbuf, size_t errlen)
{
	pathref_t *refs;
	int nrefs;
	int i;
	int err = 0;

	refs = tree_paths(base, &nrefs);

	for (i = 0; i < nrefs; i++) {
		const rt_tree_pool_t *pool = refs[i].pr_pool;
		const char *path = refs[i].pr_path;
		const char *leaf = rt_path_leaf(path);
		uint64_t dirobj, obj;
		kment_t *e;

		/*
		 * The root already exists; the fixture's root key just
		 * gets bound to it.  Every tree uses the same root
		 * identity, which is what makes untouched roots pair.
		 */
		if (strcmp(path, "/") == 0) {
			kmap_add(km, pool->rtp_key, root, 1, pool->rtp_token,
			    root);
			live_add(lv, "/", pool->rtp_key);
			continue;
		}

		if (pool->rtp_isdir && pool->rtp_token[0] != '\0') {
			BUILD_ERR(ENOTSUP, "base directory %s carries a "
			    "content token, which the harness has no way to "
			    "set", path);
		}

		err = parent_obj_of(lv, km, root, path, &dirobj);
		if (err != 0)
			BUILD_ERR(err, "base %s: parent directory missing",
			    path);

		e = kmap_find(km, pool->rtp_key);
		if (e != NULL) {
			err = rt_add_hardlink(os, dirobj, leaf, e->km_obj);
			if (err != 0)
				BUILD_ERR(err, "base %s: hardlink failed",
				    path);
		} else if (pool->rtp_isdir) {
			err = rt_create_dir(os, dirobj, leaf, &obj);
			if (err != 0)
				BUILD_ERR(err, "base %s: mkdir failed", path);
			kmap_add(km, pool->rtp_key, obj, 1, pool->rtp_token,
			    dirobj);
		} else {
			err = rt_create_file(os, dirobj, leaf,
			    pool->rtp_token, strlen(pool->rtp_token), &obj);
			if (err != 0)
				BUILD_ERR(err, "base %s: create failed", path);
			kmap_add(km, pool->rtp_key, obj, 0, pool->rtp_token,
			    dirobj);
		}

		live_add(lv, path, pool->rtp_key);
	}

out:
	free(refs);
	return (err);
}

/*
 * ------------------------------------------------------------------
 * One side's delta
 * ------------------------------------------------------------------
 */

static int
apply_side(const char *dsname, const rt_tree_t *base, const rt_tree_t *side,
    const kmap_t *base_km, char *errbuf, size_t errlen)
{
	rt_ds_t d;
	kmap_t km;
	live_t lv;
	pathref_t *refs = NULL;
	pathref_t *brefs = NULL;
	int nrefs = 0, nbrefs = 0;
	int scratch = 0;
	uint64_t root;
	int i, j;
	int err;

	(void) memset(&lv, 0, sizeof (lv));
	(void) memset(&km, 0, sizeof (km));

	err = rt_open(dsname, &d);
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "cannot open %s", dsname);
		return (err);
	}
	root = d.rtd_root;

	/* The clone starts as base, object numbers and all. */
	kmap_copy(&km, base_km);
	brefs = tree_paths(base, &nbrefs);
	for (i = 0; i < nbrefs; i++)
		live_add(&lv, brefs[i].pr_path, brefs[i].pr_pool->rtp_key);

	refs = tree_paths(side, &nrefs);

	/* --- A. placements, shallowest first --- */
	for (i = 0; i < nrefs; i++) {
		const rt_tree_pool_t *pool = refs[i].pr_pool;
		const char *path = refs[i].pr_path;
		const char *leaf = rt_path_leaf(path);
		uint64_t dirobj, obj;
		kment_t *e;
		int li;
		int donor = -1;

		if (strcmp(path, "/") == 0) {
			li = live_find(&lv, "/");
			if (li < 0 || strcmp(lv.l_ents[li].le_key,
			    pool->rtp_key) != 0) {
				BUILD_ERR(ENOTSUP, "%s gives the root a "
				    "different identity (%s) than base; the "
				    "root dnode cannot be replaced", dsname,
				    pool->rtp_key);
			}
			continue;
		}

		if (pool->rtp_isdir && pool->rtp_token[0] != '\0') {
			BUILD_ERR(ENOTSUP, "%s: directory %s carries a "
			    "content token, which the harness has no way to "
			    "set", dsname, path);
		}

		li = live_find(&lv, path);
		if (li >= 0 && strcmp(lv.l_ents[li].le_key,
		    pool->rtp_key) == 0)
			continue;		/* already right */

		/*
		 * Occupied by the wrong dnode.  Move the occupant to a
		 * scratch name at the root; its own key's turn will
		 * collect it, or phase B will remove it.  Without this
		 * a pair of names that swap places is unbuildable.
		 */
		if (li >= 0) {
			char scratchp[64];
			uint64_t odir;

			(void) snprintf(scratchp, sizeof (scratchp),
			    "/.rt-scratch-%d", scratch++);
			err = parent_obj_of(&lv, &km, root, path, &odir);
			if (err != 0)
				BUILD_ERR(err, "%s: %s has no parent",
				    dsname, path);
			err = rename_entry(d.rtd_os,
			    kmap_find(&km, lv.l_ents[li].le_key), odir,
			    rt_path_leaf(path), root, scratchp + 1);
			if (err != 0)
				BUILD_ERR(err, "%s: cannot vacate %s",
				    dsname, path);
			live_repath(&lv, path, scratchp);
		}

		err = parent_obj_of(&lv, &km, root, path, &dirobj);
		if (err != 0)
			BUILD_ERR(err, "%s: %s: parent directory missing",
			    dsname, path);

		e = kmap_find(&km, pool->rtp_key);
		if (e == NULL) {
			/* A dnode this side alone has. */
			if (pool->rtp_isdir) {
				err = rt_create_dir(d.rtd_os, dirobj, leaf,
				    &obj);
				if (err != 0)
					BUILD_ERR(err, "%s: mkdir %s failed",
					    dsname, path);
				kmap_add(&km, pool->rtp_key, obj, 1,
				    pool->rtp_token, dirobj);
			} else {
				err = rt_create_file(d.rtd_os, dirobj, leaf,
				    pool->rtp_token,
				    strlen(pool->rtp_token), &obj);
				if (err != 0)
					BUILD_ERR(err, "%s: create %s failed",
					    dsname, path);
				kmap_add(&km, pool->rtp_key, obj, 0,
				    pool->rtp_token, dirobj);
			}
			live_add(&lv, path, pool->rtp_key);
			continue;
		}

		/*
		 * The dnode is already here under some other name.  If
		 * one of those names is not wanted, this is a MOVE and
		 * must be a rename: unlink-then-create would break the
		 * identity the fixture is asserting.  If every current
		 * name is wanted too, the fixture is adding a link.
		 */
		for (j = 0; j < lv.l_n; j++) {
			if (strcmp(lv.l_ents[j].le_key, pool->rtp_key) != 0)
				continue;
			if (target_holds(refs, nrefs, lv.l_ents[j].le_path,
			    pool->rtp_key))
				continue;
			donor = j;
			break;
		}

		if (donor >= 0) {
			char *from = rt_xstrdup(lv.l_ents[donor].le_path);
			uint64_t fdir;

			err = parent_obj_of(&lv, &km, root, from, &fdir);
			if (err == 0) {
				err = rename_entry(d.rtd_os, e, fdir,
				    rt_path_leaf(from), dirobj, leaf);
			}
			if (err != 0) {
				free(from);
				BUILD_ERR(err, "%s: rename %s to %s failed",
				    dsname, from, path);
			}
			live_repath(&lv, from, path);
			free(from);
		} else {
			if (e->km_isdir) {
				BUILD_ERR(ENOTSUP, "%s: %s would give "
				    "directory %s a second name "
				    "(Definition 1.3)", dsname, path,
				    pool->rtp_key);
			}
			err = rt_add_hardlink(d.rtd_os, dirobj, leaf,
			    e->km_obj);
			if (err != 0)
				BUILD_ERR(err, "%s: hardlink %s failed",
				    dsname, path);
			live_add(&lv, path, pool->rtp_key);
		}
	}

	/* --- B. removals, deepest first --- */
	for (;;) {
		int victim = -1;
		int vdepth = -1;
		uint64_t dirobj;

		for (i = 0; i < lv.l_n; i++) {
			int dep;

			if (strcmp(lv.l_ents[i].le_path, "/") == 0)
				continue;
			if (target_holds(refs, nrefs, lv.l_ents[i].le_path,
			    NULL))
				continue;
			dep = rt_path_depth(lv.l_ents[i].le_path);
			if (dep > vdepth) {
				vdepth = dep;
				victim = i;
			}
		}
		if (victim < 0)
			break;

		err = parent_obj_of(&lv, &km, root, lv.l_ents[victim].le_path,
		    &dirobj);
		if (err != 0)
			BUILD_ERR(err, "%s: %s has no parent", dsname,
			    lv.l_ents[victim].le_path);
		err = rt_remove_entry(d.rtd_os, dirobj,
		    rt_path_leaf(lv.l_ents[victim].le_path));
		if (err != 0)
			BUILD_ERR(err, "%s: remove %s failed", dsname,
			    lv.l_ents[victim].le_path);
		live_del(&lv, victim);
	}

	/* --- C. content, last --- */
	for (i = 0; i < side->rtt_npools; i++) {
		const rt_tree_pool_t *pool = &side->rtt_pools[i];
		kment_t *e = kmap_find(&km, pool->rtp_key);

		if (e == NULL || e->km_isdir)
			continue;
		if (strcmp(e->km_token, pool->rtp_token) == 0)
			continue;
		err = rt_edit_file(d.rtd_os, e->km_obj, pool->rtp_token,
		    strlen(pool->rtp_token));
		if (err != 0)
			BUILD_ERR(err, "%s: edit of pool %s failed", dsname,
			    pool->rtp_key);
		free(e->km_token);
		e->km_token = rt_xstrdup(pool->rtp_token);
	}

	err = 0;
out:
	rt_close(&d);
	free(refs);
	free(brefs);
	live_free(&lv);
	kmap_free(&km);
	return (err);
}

/*
 * ------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------
 */

int
rt_tree_materialize(const rt_spec_t *sp, char *errbuf, size_t errlen)
{
	kmap_t base_km;
	live_t base_lv;
	rt_ds_t d;
	int err;

	(void) memset(&base_km, 0, sizeof (base_km));
	(void) memset(&base_lv, 0, sizeof (base_lv));
	if (errlen > 0)
		errbuf[0] = '\0';

	if (sp->rts_nerrors > 0) {
		(void) snprintf(errbuf, errlen, "fixture does not parse (%d "
		    "error(s), first: %s)", sp->rts_nerrors,
		    sp->rts_errors[0]);
		return (EINVAL);
	}

	err = rt_scaffold_empty_base();
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "cannot create pool");
		return (err);
	}

	err = rt_open(RT_DS_SRC, &d);
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "cannot open " RT_DS_SRC);
		goto out;
	}
	err = build_base(d.rtd_os, d.rtd_root,
	    &sp->rts_trees[RT_TREE_BASE], &base_km, &base_lv, errbuf, errlen);
	rt_close(&d);
	if (err != 0)
		goto out;

	err = rt_scaffold_snap_and_clone();
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "snapshot and clone failed");
		goto out;
	}

	/*
	 * Each side's tree goes into the dataset named for that side's
	 * role.  This used to need a paragraph explaining which
	 * positional dataset played which role; now the names say it,
	 * and a fixture built into the wrong side would have to be
	 * written wrong in a way that reads wrong.
	 */
	err = apply_side(RT_DS_ONTO, &sp->rts_trees[RT_TREE_BASE],
	    &sp->rts_trees[RT_TREE_ONTO], &base_km, errbuf, errlen);
	if (err != 0)
		goto out;

	err = apply_side(RT_DS_OFFOF, &sp->rts_trees[RT_TREE_BASE],
	    &sp->rts_trees[RT_TREE_OFFOF], &base_km, errbuf, errlen);
	if (err != 0)
		goto out;

	/*
	 * The engine reads snapshots, not heads, so the two sides are
	 * pinned here.  Sync first: the snapshot has to capture every
	 * edit above, and a snapshot of a half-written side would fail
	 * in a way that looks like an engine bug.
	 */
	rt_sync_pool();

	err = rt_snapshot(RT_DS_ONTO, "fixture");
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "cannot snapshot onto");
		goto out;
	}
	err = rt_snapshot(RT_DS_OFFOF, "fixture");
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "cannot snapshot off-of");
		goto out;
	}

	rt_sync_pool();
	err = 0;
out:
	live_free(&base_lv);
	kmap_free(&base_km);
	return (err);
}
