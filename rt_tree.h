// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * The .tree fixture format: parsed spec, in C.
 *
 * A .tree file states a whole rebase scenario -- the three input
 * trees, the output that should come out of them, and the things an
 * output tree cannot say -- as one artifact.  SPEC.md in the
 * decide-demo directory is the normative grammar; this is a second
 * implementation of it, and devcheck/treecheck.sh proves the two
 * agree on the corpus rather than taking anyone's word for it.
 *
 * This header deliberately pulls in NOTHING from ZFS.  The parser is
 * ordinary C99 over ordinary text, which is what lets it build and
 * run on a machine with no ZFS headers -- see devcheck/treedump.c.
 * The parts that need a pool live in rt_tree_build.c, and the parts
 * that need a decision record live in rt_tree_check.c.
 *
 * Vocabulary note: the trees are base, onto and off-of, never left
 * and right.  Onto is the substrate the output is built on; off-of
 * is the side whose changes are replayed.  In harness terms that is
 * RT_DS_RIGHT and RT_DS_LEFT respectively, which is the one place
 * the two vocabularies have to touch (rt_tree_build.c says so again
 * where it matters).
 */

#ifndef	_RT_TREE_H
#define	_RT_TREE_H

#include <stdio.h>
#include <stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Tree slots.  The first three are inputs; the fourth is gold data
 * rather than input, and is optional.  The order is contract: it is
 * the order the canonical dump emits and the order a reader expects.
 */
#define	RT_TREE_BASE		0
#define	RT_TREE_ONTO		1
#define	RT_TREE_OFFOF		2
#define	RT_TREE_EXPECTED	3
#define	RT_NTREES		4

/* Longest "<idx>-<txg>" we will format, plus room for the NUL. */
#define	RT_KEYLEN		48

/*
 * One pool: a dnode identity together with every name that reaches
 * it (Definition 1.1).  Names are full paths and are sorted; keys
 * sort numerically on (idx, txg), never by string collation, so an
 * ordering never depends on decimal spelling.
 */
typedef struct rt_tree_pool {
	uint64_t	rtp_idx;
	uint64_t	rtp_txg;
	char		rtp_key[RT_KEYLEN];
	int		rtp_isdir;
	char		*rtp_token;	/* content token, never NULL */
	char		**rtp_names;	/* sorted full paths */
	int		rtp_nnames;
} rt_tree_pool_t;

typedef struct rt_tree {
	rt_tree_pool_t	*rtt_pools;	/* sorted by (idx, txg) */
	int		rtt_npools;
	int		rtt_present;	/* the block appeared at all */
} rt_tree_t;

/* What an expected tree cannot say for itself. */
#define	RT_EXP_CLEAN		1
#define	RT_EXP_CONFLICT		2
#define	RT_EXP_QUARANTINED	3

typedef struct rt_expect {
	int		rte_kind;
	char		*rte_arg;	/* conflict kind or path */
	int		rte_line;
} rt_expect_t;

typedef struct rt_spec {
	char		*rts_path;	/* source file, for messages */
	char		*rts_title;
	rt_tree_t	rts_trees[RT_NTREES];
	rt_expect_t	*rts_expects;
	int		rts_nexpects;
	char		**rts_errors;	/* "line N: message", sorted */
	int		rts_nerrors;
} rt_spec_t;

/*
 * Parse.  Both return 0 when the text parsed with no errors and
 * EINVAL when it did not; either way the spec is populated and must
 * be freed, because the error list is the interesting part of a
 * failed parse.  A NULL or unreadable path returns its errno.
 */
int rt_spec_parse_text(const char *text, const char *origin,
    rt_spec_t *sp);
int rt_spec_parse_file(const char *path, rt_spec_t *sp);
void rt_spec_free(rt_spec_t *sp);

/*
 * The canonical dump: a total, stable rendering of everything the
 * parse decided.  devcheck/treecheck.sh diffs this against the same
 * dump taken from the reference parser in JavaScript, which is what
 * makes "the two implementations agree" a checked claim rather than
 * an intention.  Any change to this format changes that gate, so
 * devcheck/tree-canon.js has to move with it.
 */
void rt_spec_dump(const rt_spec_t *sp, FILE *out);

/* Lookups.  Both return NULL when there is no such thing. */
const rt_tree_pool_t *rt_tree_by_name(const rt_tree_t *t,
    const char *name);
const rt_tree_pool_t *rt_tree_by_key(const rt_tree_t *t,
    const char *key);

/* Human-readable slot name ("base", "onto", "off-of", "expected"). */
const char *rt_tree_slot_name(int slot);

/*
 * Shapes a fixture knows about itself, which the engine's walk census
 * restates: how many names one tree holds, and how many distinct
 * names the three INPUT trees hold between them.  Kept here, with the
 * parser, because they are arithmetic over parsed text and belong
 * where they can be run rather than only compiled.
 */
int rt_tree_nnames(const rt_tree_t *t);
int rt_spec_union_names(const rt_spec_t *sp);

/*
 * Allocation that aborts rather than returning NULL, shared with the
 * materializer.  A test binary that cannot get a few kilobytes has
 * nothing useful left to report, and every call site would otherwise
 * carry a branch that is never taken and never tested.
 */
void *rt_xmalloc(size_t n);
void *rt_xrealloc(void *p, size_t n);
char *rt_xstrdup(const char *s);

/*
 * Path helpers, shared with the materializer.  parent_path returns 0
 * and writes the parent into buf, or ENOENT for "/" which has none.
 */
int rt_path_parent(const char *path, char *buf, size_t buflen);
const char *rt_path_leaf(const char *path);
/* Directory depth: "/" is 0, "/a" is 1, "/a/b" is 2. */
int rt_path_depth(const char *path);

#ifdef	__cplusplus
}
#endif

#endif	/* _RT_TREE_H */
