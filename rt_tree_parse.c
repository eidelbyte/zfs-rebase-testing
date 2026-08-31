// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * The .tree parser: a second implementation of the grammar in
 * decide-demo/SPEC.md, in C, with no ZFS dependency at all.
 *
 * Why a second implementation exists.  The fixtures are shared with
 * the reference engine in JavaScript, byte for byte -- one artifact,
 * two readers.  Two readers is only a virtue if they agree, so this
 * file is written to mirror the reference parser's decisions
 * exactly, down to the wording of its error messages, and
 * devcheck/treecheck.sh diffs the canonical dumps of both over the
 * whole corpus.  When the reference parser changes, that gate fails
 * here, which is the point: a fixture that means two different
 * things to two engines is worse than no fixture.
 *
 * The one divergence left is wording, on the tree names "left" and
 * "right".  The reference parser used to accept them as aliases;
 * both parsers refuse them now and differ only in what they say.
 *
 * They are refused because they carry no direction.  It is tempting
 * to record instead that the old aliases were BACKWARDS, and that is
 * the mistake to avoid: two people who had both read this project
 * closely turned out to hold opposite mappings, and each found the
 * same fixture plausible.  There was never a correct direction to
 * preserve.  That is why the answer is to delete the words rather
 * than fix them, and why this parser's message says the name does
 * not identify a side rather than that it identifies the wrong one.
 * devcheck/treecheck.sh checks both refusals, so "only the wording
 * differs" stays verified rather than merely remembered.
 *
 * Allocation failure aborts, as it does elsewhere in the harness: a
 * test binary that cannot get memory has nothing useful to report.
 */

#include "rt_tree.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/*
 * ------------------------------------------------------------------
 * Small allocation and array helpers
 * ------------------------------------------------------------------
 */

void *
rt_xmalloc(size_t n)
{
	void *p = malloc(n == 0 ? 1 : n);

	if (p == NULL) {
		(void) fprintf(stderr, "rt_tree: out of memory\n");
		abort();
	}
	return (p);
}

void *
rt_xrealloc(void *p, size_t n)
{
	void *np = realloc(p, n == 0 ? 1 : n);

	if (np == NULL) {
		(void) fprintf(stderr, "rt_tree: out of memory\n");
		abort();
	}
	return (np);
}

char *
rt_xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = rt_xmalloc(n);

	(void) memcpy(p, s, n);
	return (p);
}

static char *
xstrndup(const char *s, size_t n)
{
	char *p = rt_xmalloc(n + 1);

	(void) memcpy(p, s, n);
	p[n] = '\0';
	return (p);
}

/*
 * Fixtures are tens of lines, so an append that reallocates every
 * time is not worth avoiding and is one fewer thing to get wrong.
 */
#define	APPEND(arr, count, elemtype)					\
	((arr) = rt_xrealloc((arr), (size_t)((count) + 1) *		\
	    sizeof (elemtype)), &(arr)[(count)++])

/*
 * ------------------------------------------------------------------
 * Errors
 * ------------------------------------------------------------------
 *
 * Stored pre-rendered as "line N: message" so the dump, the sort and
 * the reporting all handle one string.  Line 0 means "about the tree
 * as a whole", which is where the reference parser puts the checks
 * that are not about any single line.
 */

static void
add_error(rt_spec_t *sp, int line, const char *fmt, ...)
{
	char msg[512];
	char full[600];
	va_list ap;
	char **slot;

	va_start(ap, fmt);
	(void) vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	(void) snprintf(full, sizeof (full), "line %d: %s", line, msg);
	slot = APPEND(sp->rts_errors, sp->rts_nerrors, char *);
	*slot = rt_xstrdup(full);
}

/*
 * Sort by line then message, matching the reference parser.  The
 * rendered form starts with the line number, so a plain string sort
 * would order line 10 before line 2; parse the number back out.
 */
static int
error_cmp(const void *a, const void *b)
{
	const char *sa = *(char * const *)a;
	const char *sb = *(char * const *)b;
	long la = strtol(sa + 5, NULL, 10);
	long lb = strtol(sb + 5, NULL, 10);

	if (la != lb)
		return (la < lb ? -1 : 1);
	return (strcmp(sa, sb));
}

/*
 * ------------------------------------------------------------------
 * Path algebra.  Names are full paths (Definition 1.1).
 * ------------------------------------------------------------------
 */

int
rt_path_parent(const char *path, char *buf, size_t buflen)
{
	const char *slash;
	size_t n;

	if (path == NULL || strcmp(path, "/") == 0)
		return (ENOENT);

	slash = strrchr(path, '/');
	if (slash == NULL)
		return (ENOENT);
	if (slash == path) {
		if (buflen < 2)
			return (ENAMETOOLONG);
		(void) strcpy(buf, "/");
		return (0);
	}

	n = (size_t)(slash - path);
	if (n + 1 > buflen)
		return (ENAMETOOLONG);
	(void) memcpy(buf, path, n);
	buf[n] = '\0';
	return (0);
}

const char *
rt_path_leaf(const char *path)
{
	const char *slash;

	if (path == NULL || strcmp(path, "/") == 0)
		return ("/");
	slash = strrchr(path, '/');
	return (slash == NULL ? path : slash + 1);
}

int
rt_path_depth(const char *path)
{
	int d = 0;
	const char *p;

	if (path == NULL || strcmp(path, "/") == 0)
		return (0);
	for (p = path; *p != '\0'; p++) {
		if (*p == '/')
			d++;
	}
	return (d);
}

static char *
join_path(const char *parent, const char *leaf)
{
	size_t pl = strlen(parent);
	size_t ll = strlen(leaf);
	char *out;

	if (strcmp(parent, "/") == 0) {
		out = rt_xmalloc(ll + 2);
		out[0] = '/';
		(void) memcpy(out + 1, leaf, ll + 1);
		return (out);
	}
	out = rt_xmalloc(pl + ll + 2);
	(void) memcpy(out, parent, pl);
	out[pl] = '/';
	(void) memcpy(out + pl + 1, leaf, ll + 1);
	return (out);
}

/*
 * ------------------------------------------------------------------
 * mtree(5) name escapes
 * ------------------------------------------------------------------
 *
 * A file name may hold any byte but '/' and NUL, which is more than a
 * line-oriented fixture format can carry literally: a newline ends the
 * line, an '=' would be read as the content separator, an edge space
 * is trimmed away, and a byte outside printable ASCII cannot appear at
 * all under this project's ASCII rule.
 *
 * So names use mtree(5)'s encoding -- a backslash and exactly three
 * octal digits -- rather than inventing one.  It is already specified,
 * already implemented in base, reversible for arbitrary bytes, and
 * ASCII by construction.  Using the SAME encoding the output manifest
 * uses is the real prize: a path decoded from a fixture and a path
 * decoded from a manifest are comparable with nothing in between.
 *
 * This is an extension with one compatibility edge, and it is worth
 * stating plainly rather than calling the whole change additive: a
 * lone backslash used to be a literal and is now malformed.  Neither
 * corpus contains one, which is what makes that safe.
 */

#define	RT_UNESC_OK		0
#define	RT_UNESC_BADSLASH	1
#define	RT_UNESC_NUL		2
#define	RT_UNESC_RANGE		3

#define	RT_IS_OCTAL(c)	((c) >= '0' && (c) <= '7')

/*
 * Decode into a fresh string, so a failure leaves the caller's
 * original intact to quote in the complaint.  Returns one of the
 * RT_UNESC_ codes; *outp is set only on success.
 */
static int
unescape_name(const char *in, char **outp)
{
	char *out = rt_xmalloc(strlen(in) + 1);
	const char *r = in;
	char *w = out;

	while (*r != '\0') {
		unsigned int v;

		if (*r != '\\') {
			*w++ = *r++;
			continue;
		}
		/*
		 * Short-circuit order matters: a string ending in a
		 * backslash has '\0' at r[1], which is not octal, so
		 * this never reads past the end.
		 */
		if (!RT_IS_OCTAL(r[1]) || !RT_IS_OCTAL(r[2]) ||
		    !RT_IS_OCTAL(r[3])) {
			free(out);
			return (RT_UNESC_BADSLASH);
		}
		v = (unsigned int)(r[1] - '0') * 64 +
		    (unsigned int)(r[2] - '0') * 8 +
		    (unsigned int)(r[3] - '0');
		if (v == 0) {
			free(out);
			return (RT_UNESC_NUL);
		}
		/*
		 * Three octal digits reach 511, and a byte does not.
		 * Rejected rather than truncated: \400 would otherwise
		 * become NUL here and a 256-code character in the
		 * reference parser, which is a divergence on a fixture
		 * nobody would think to write.
		 */
		if (v > 255) {
			free(out);
			return (RT_UNESC_RANGE);
		}
		*w++ = (char)v;
		r += 4;
	}
	*w = '\0';
	*outp = out;
	return (RT_UNESC_OK);
}

/*
 * Re-encode for the canonical dump, so the dump stays printable ASCII
 * whatever bytes a name holds -- and so the cross-parser diff covers
 * the escaping itself rather than stopping at its edge.
 */
static void
fput_escaped(FILE *out, const char *s)
{
	const unsigned char *p = (const unsigned char *)s;

	for (; *p != '\0'; p++) {
		if (*p == '\\' || *p < 0x20 || *p > 0x7e)
			(void) fprintf(out, "\\%03o", *p);
		else
			(void) fputc((int)*p, out);
	}
}

/*
 * ------------------------------------------------------------------
 * Raw node lines, before they become pools
 * ------------------------------------------------------------------
 */

typedef struct raw_node {
	int		rn_lineno;
	int		rn_indent;
	uint64_t	rn_idx;
	uint64_t	rn_txg;
	char		*rn_leaf;
	char		*rn_token;	/* NULL when the line had no '=' */
	int		rn_forced_dir;
	int		rn_parent;	/* -1 at top level */
	int		rn_haschild;
	char		*rn_path;
} raw_node_t;

typedef struct raw_tree {
	raw_node_t	*rt_nodes;
	int		rt_nnodes;
	int		rt_seen;	/* a "tree <slot>" line appeared */
} raw_tree_t;

static int
name_cmp(const void *a, const void *b)
{
	return (strcmp(*(char * const *)a, *(char * const *)b));
}

static int
pool_cmp(const void *a, const void *b)
{
	const rt_tree_pool_t *pa = a;
	const rt_tree_pool_t *pb = b;

	if (pa->rtp_idx != pb->rtp_idx)
		return (pa->rtp_idx < pb->rtp_idx ? -1 : 1);
	if (pa->rtp_txg != pb->rtp_txg)
		return (pa->rtp_txg < pb->rtp_txg ? -1 : 1);
	return (strcmp(pa->rtp_key, pb->rtp_key));
}

/*
 * ------------------------------------------------------------------
 * buildTree: raw lines to pools (Definitions 1.1 and 1.3)
 * ------------------------------------------------------------------
 */

/* Grouping scratch: the lines that share one dnode identity. */
typedef struct key_group {
	char		kg_key[RT_KEYLEN];
	uint64_t	kg_idx;
	uint64_t	kg_txg;
	int		*kg_lines;	/* indices into the node array */
	int		kg_nlines;
	int		kg_implicit_root;
} key_group_t;

static void
build_tree(rt_spec_t *sp, raw_node_t *nodes, int nn, rt_tree_t *out)
{
	int *stack = NULL;
	int nstack = 0;
	int i, j;
	int ntops = 0;
	int first_top = -1;
	int root_index = -1;
	int need_implicit_root = 1;
	key_group_t *groups = NULL;
	int ngroups = 0;
	struct seen_name {
		const char *sn_name;
		const char *sn_key;
	} *seen = NULL;
	int nseen = 0;

	/*
	 * Parents by indentation: the parent is the nearest preceding
	 * line indented strictly less.  SPEC.md is deliberately
	 * lenient about the step size, so this never assumes two.
	 */
	if (nn > 0)
		stack = rt_xmalloc((size_t)nn * sizeof (int));
	for (i = 0; i < nn; i++) {
		while (nstack > 0 &&
		    nodes[stack[nstack - 1]].rn_indent >= nodes[i].rn_indent)
			nstack--;
		nodes[i].rn_parent = nstack > 0 ? stack[nstack - 1] : -1;
		stack[nstack++] = i;
	}
	free(stack);

	for (i = 0; i < nn; i++) {
		if (nodes[i].rn_parent >= 0)
			nodes[nodes[i].rn_parent].rn_haschild = 1;
	}

	for (i = 0; i < nn; i++) {
		if (nodes[i].rn_parent == -1) {
			if (ntops == 0)
				first_top = i;
			ntops++;
		}
	}

	/*
	 * Root: a single top-level "/" line is the root pool.
	 * Otherwise the implicit 1-1 root is injected, and every tree
	 * uses the same implicit identity so untouched roots pair.
	 */
	if (ntops == 1 && strcmp(nodes[first_top].rn_leaf, "/") == 0) {
		root_index = first_top;
		need_implicit_root = 0;
	}
	if (nn == 0)
		need_implicit_root = 0;

	for (i = 0; i < nn; i++) {
		if (strcmp(nodes[i].rn_leaf, "/") == 0 && i != root_index) {
			char rename[64];

			add_error(sp, nodes[i].rn_lineno, "leaf '/' is only "
			    "legal as the single top-level root line");
			(void) snprintf(rename, sizeof (rename),
			    "_bad_root_%d", i);
			free(nodes[i].rn_leaf);
			nodes[i].rn_leaf = rt_xstrdup(rename);
		}
	}

	for (i = 0; i < nn; i++) {
		if (i == root_index) {
			nodes[i].rn_path = rt_xstrdup("/");
			continue;
		}
		nodes[i].rn_path = join_path(nodes[i].rn_parent == -1 ? "/" :
		    nodes[nodes[i].rn_parent].rn_path, nodes[i].rn_leaf);
	}

	/* Group lines by dnode identity. */
	for (i = 0; i < nn; i++) {
		char key[RT_KEYLEN];
		key_group_t *g = NULL;

		(void) snprintf(key, sizeof (key), "%llu-%llu",
		    (unsigned long long)nodes[i].rn_idx,
		    (unsigned long long)nodes[i].rn_txg);
		for (j = 0; j < ngroups; j++) {
			if (strcmp(groups[j].kg_key, key) == 0) {
				g = &groups[j];
				break;
			}
		}
		if (g == NULL) {
			g = APPEND(groups, ngroups, key_group_t);
			(void) memset(g, 0, sizeof (*g));
			(void) snprintf(g->kg_key, sizeof (g->kg_key), "%s",
			    key);
			g->kg_idx = nodes[i].rn_idx;
			g->kg_txg = nodes[i].rn_txg;
		}
		*APPEND(g->kg_lines, g->kg_nlines, int) = i;
	}

	if (need_implicit_root) {
		key_group_t *g = NULL;

		for (j = 0; j < ngroups; j++) {
			if (strcmp(groups[j].kg_key, "1-1") == 0) {
				g = &groups[j];
				break;
			}
		}
		if (g != NULL) {
			add_error(sp, nn > 0 ? nodes[0].rn_lineno : 0,
			    "implicit root identity 1-1 is already used by "
			    "an explicit node");
		} else {
			g = APPEND(groups, ngroups, key_group_t);
			(void) memset(g, 0, sizeof (*g));
			(void) strcpy(g->kg_key, "1-1");
			g->kg_idx = 1;
			g->kg_txg = 1;
			g->kg_implicit_root = 1;
		}
	}

	/* Groups become pools, ordered by identity. */
	for (i = 0; i < ngroups; i++) {
		key_group_t *g = &groups[i];
		rt_tree_pool_t *pool;
		int type = -1;		/* -1 unset, 0 file, 1 dir */
		char *token = NULL;

		pool = APPEND(out->rtt_pools, out->rtt_npools,
		    rt_tree_pool_t);
		(void) memset(pool, 0, sizeof (*pool));
		pool->rtp_idx = g->kg_idx;
		pool->rtp_txg = g->kg_txg;
		(void) snprintf(pool->rtp_key, sizeof (pool->rtp_key),
		    "%s", g->kg_key);

		if (g->kg_implicit_root) {
			*APPEND(pool->rtp_names, pool->rtp_nnames, char *) =
			    rt_xstrdup("/");
			pool->rtp_isdir = 1;
			pool->rtp_token = rt_xstrdup("");
			continue;
		}

		for (j = 0; j < g->kg_nlines; j++) {
			raw_node_t *ln = &nodes[g->kg_lines[j]];
			int ln_type;

			*APPEND(pool->rtp_names, pool->rtp_nnames, char *) =
			    rt_xstrdup(ln->rn_path);

			/*
			 * A node with children is a directory; a trailing
			 * '/' forces one, which is how an empty directory
			 * is written; and the root is a directory always,
			 * even written childless.
			 */
			ln_type = (ln->rn_haschild || ln->rn_forced_dir ||
			    g->kg_lines[j] == root_index) ? 1 : 0;
			if (type == -1) {
				type = ln_type;
			} else if (type != ln_type) {
				add_error(sp, ln->rn_lineno, "pool %s is a "
				    "directory on one line and a file on "
				    "another", g->kg_key);
			}

			if (ln->rn_token != NULL) {
				if (token != NULL &&
				    strcmp(token, ln->rn_token) != 0) {
					add_error(sp, ln->rn_lineno, "pool %s "
					    "has conflicting content tokens",
					    g->kg_key);
				}
				free(token);
				token = rt_xstrdup(ln->rn_token);
			}
		}

		pool->rtp_isdir = (type == 1);
		pool->rtp_token = token != NULL ? token : rt_xstrdup("");

		if (pool->rtp_isdir && pool->rtp_nnames > 1) {
			add_error(sp, nodes[g->kg_lines[1]].rn_lineno,
			    "directory pool %s holds two names; directories "
			    "cannot be hard-linked (Definition 1.3)",
			    g->kg_key);
		}

		/* Sort and de-duplicate the pool's names. */
		qsort(pool->rtp_names, (size_t)pool->rtp_nnames,
		    sizeof (char *), name_cmp);
		for (j = 1; j < pool->rtp_nnames; j++) {
			if (strcmp(pool->rtp_names[j - 1],
			    pool->rtp_names[j]) != 0)
				continue;
			free(pool->rtp_names[j]);
			(void) memmove(&pool->rtp_names[j],
			    &pool->rtp_names[j + 1],
			    (size_t)(pool->rtp_nnames - j - 1) *
			    sizeof (char *));
			pool->rtp_nnames--;
			j--;
		}
	}

	qsort(out->rtt_pools, (size_t)out->rtt_npools,
	    sizeof (rt_tree_pool_t), pool_cmp);

	/* One name, one pool. */
	for (i = 0; i < out->rtt_npools; i++) {
		rt_tree_pool_t *pool = &out->rtt_pools[i];

		for (j = 0; j < pool->rtp_nnames; j++) {
			struct seen_name *slot;
			int k;
			int dup = -1;

			for (k = 0; k < nseen; k++) {
				if (strcmp(seen[k].sn_name,
				    pool->rtp_names[j]) == 0) {
					dup = k;
					break;
				}
			}
			if (dup >= 0) {
				/*
				 * The complaint names the pool that held
				 * the name a moment ago, not the first
				 * one ever to hold it, so a third claimant
				 * is reported against the second.  The
				 * reference parser overwrites its holder
				 * map here; do the same or the two diverge
				 * on the third duplicate.
				 */
				add_error(sp, 0, "name %s is held by two "
				    "pools (%s and %s)", pool->rtp_names[j],
				    seen[dup].sn_key, pool->rtp_key);
				seen[dup].sn_key = pool->rtp_key;
				continue;
			}
			slot = APPEND(seen, nseen, struct seen_name);
			slot->sn_name = pool->rtp_names[j];
			slot->sn_key = pool->rtp_key;
		}
	}

	/*
	 * Definition 1.3: prefix closure and directory parents.
	 * Walked over the DISTINCT names, in the order they were
	 * first claimed, because a name claimed by two pools is one
	 * name and deserves one complaint.
	 */
	for (i = 0; i < nseen; i++) {
		char par[1024];

		if (rt_path_parent(seen[i].sn_name, par, sizeof (par)) != 0)
			continue;
		if (rt_tree_by_name(out, par) == NULL) {
			add_error(sp, 0, "name %s has no parent %s (tree is "
			    "not prefix-closed)", seen[i].sn_name, par);
		}
	}

	free(seen);
	for (i = 0; i < ngroups; i++)
		free(groups[i].kg_lines);
	free(groups);
}

/*
 * ------------------------------------------------------------------
 * Line scanning
 * ------------------------------------------------------------------
 */

/* "title" followed by a non-word character or end of string. */
static int
word_is(const char *body, const char *word, const char **restp)
{
	size_t n = strlen(word);
	char c;

	if (strncmp(body, word, n) != 0)
		return (0);
	c = body[n];
	if (c != '\0' && (isalnum((unsigned char)c) || c == '_'))
		return (0);
	*restp = body + n;
	return (1);
}

char *
trimmed(const char *s)
{
	const char *end;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		end--;
	return (xstrndup(s, (size_t)(end - s)));
}

/*
 * The reference grammar's ([A-Za-z-]+) with a trailing \s*: at least
 * one letter-or-dash, then nothing but whitespace to end of line.
 */
static int
tree_name_ok(const char *rest)
{
	int n = 0;

	while (*rest != '\0' && isspace((unsigned char)*rest))
		rest++;
	while (isalpha((unsigned char)*rest) || *rest == '-') {
		rest++;
		n++;
	}
	while (*rest != '\0' && isspace((unsigned char)*rest))
		rest++;
	return (n > 0 && *rest == '\0');
}

static int
slot_of(const char *name)
{
	if (strcmp(name, "base") == 0)
		return (RT_TREE_BASE);
	if (strcmp(name, "onto") == 0)
		return (RT_TREE_ONTO);
	if (strcmp(name, "offof") == 0 || strcmp(name, "off-of") == 0)
		return (RT_TREE_OFFOF);
	if (strcmp(name, "expected") == 0)
		return (RT_TREE_EXPECTED);
	return (-1);
}

int
rt_spec_parse_text(const char *text, const char *origin, rt_spec_t *sp)
{
	raw_tree_t raw[RT_NTREES];
	const char *p;
	int lineno = 0;
	int cur = -1;
	int i, j;

	(void) memset(sp, 0, sizeof (*sp));
	(void) memset(raw, 0, sizeof (raw));
	sp->rts_path = rt_xstrdup(origin != NULL ? origin : "<text>");
	sp->rts_title = rt_xstrdup("");

	if (text == NULL)
		text = "";

	for (p = text; ; ) {
		const char *eol = strchr(p, '\n');
		size_t rawlen = eol != NULL ? (size_t)(eol - p) : strlen(p);
		char *line;
		const char *body;
		const char *rest;
		size_t blen;
		int indent;

		lineno++;

		/* Trailing whitespace goes, which also eats a CR. */
		while (rawlen > 0 && isspace((unsigned char)p[rawlen - 1]))
			rawlen--;
		line = xstrndup(p, rawlen);

		body = line;
		while (*body != '\0' && isspace((unsigned char)*body))
			body++;
		indent = (int)(body - line);
		blen = strlen(body);

		if (blen == 0 || body[0] == '#')
			goto next;

		if (word_is(body, "title", &rest)) {
			free(sp->rts_title);
			sp->rts_title = trimmed(rest);
			goto next;
		}

		/*
		 * "expect ..." states what a tree cannot: that the run
		 * is clean, which conflict kinds it must report, and
		 * which paths come out quarantined.  Presence, pooling,
		 * content and realization are all said by the expected
		 * tree itself.
		 */
		if (strncmp(body, "expect", 6) == 0 &&
		    isspace((unsigned char)body[6])) {
			char *arg = trimmed(body + 6);
			char *sp2 = strpbrk(arg, " \t");
			rt_expect_t *ex;

			/*
			 * A quarantined argument that is missing, or that
			 * has whitespace in it, gets a message naming the
			 * remedy rather than the generic one.  The remedy
			 * differs by directive -- a space in a path is
			 * under-escaped, while a space in a conflict kind
			 * is simply wrong -- so the message names the
			 * directive rather than being generic.
			 */
			if (sp2 == NULL) {
				if (strcmp(arg, "clean") == 0) {
					ex = APPEND(sp->rts_expects,
					    sp->rts_nexpects, rt_expect_t);
					ex->rte_kind = RT_EXP_CLEAN;
					ex->rte_arg = rt_xstrdup("");
					ex->rte_line = lineno;
					free(arg);
					goto next;
				}
				if (strcmp(arg, "quarantined") == 0) {
					add_error(sp, lineno, "expect "
					    "quarantined takes one path; "
					    "write \\040 for a space in it");
					free(arg);
					goto next;
				}
			} else {
				char *verb = xstrndup(arg,
				    (size_t)(sp2 - arg));
				char *val = trimmed(sp2);
				int kind = 0;

				if (strpbrk(val, " \t") != NULL) {
					if (strcmp(verb, "quarantined") == 0) {
						add_error(sp, lineno, "expect "
						    "quarantined takes one "
						    "path; write \\040 for a "
						    "space in it");
						free(verb);
						free(val);
						free(arg);
						goto next;
					}
					kind = 0;
				} else if (strcmp(verb, "conflict") == 0) {
					kind = RT_EXP_CONFLICT;
				} else if (strcmp(verb, "quarantined") == 0) {
					kind = RT_EXP_QUARANTINED;
				}
				free(verb);

				/*
				 * A quarantined argument is a PATH, so it
				 * carries the same escapes a leaf does and
				 * has to be decoded or it could never match
				 * a decoded name.  Its '/' separators are
				 * legitimate, so unlike a leaf there is no
				 * slash check -- and \057 simply decodes to
				 * one more separator.
				 *
				 * A conflict argument is deliberately NOT
				 * decoded.  It is drawn from a closed
				 * vocabulary of five words, where an escape
				 * cannot make anything more expressible and
				 * could only admit new spellings of a word
				 * that has exactly one.
				 */
				if (kind == RT_EXP_QUARANTINED) {
					char *dec = NULL;
					int urc = unescape_name(val, &dec);

					if (urc == RT_UNESC_OK) {
						free(val);
						val = dec;
					} else {
						if (urc == RT_UNESC_BADSLASH) {
							add_error(sp, lineno,
							    "expect path '%s' "
							    "has a backslash "
							    "that is not "
							    "followed by three "
							    "octal digits",
							    val);
						} else if (urc ==
						    RT_UNESC_NUL) {
							add_error(sp, lineno,
							    "expect path "
							    "contains NUL, "
							    "which no file "
							    "name may hold");
						} else {
							add_error(sp, lineno,
							    "expect path '%s' "
							    "has an escape "
							    "above \\377, "
							    "which names no "
							    "byte", val);
						}
						free(val);
						free(arg);
						goto next;
					}
				}

				if (kind != 0) {
					ex = APPEND(sp->rts_expects,
					    sp->rts_nexpects, rt_expect_t);
					ex->rte_kind = kind;
					ex->rte_arg = val;
					ex->rte_line = lineno;
					free(arg);
					goto next;
				}
				free(val);
			}
			add_error(sp, lineno,
			    "unknown expect directive '%s'", arg);
			free(arg);
			goto next;
		}

		/*
		 * "tree <slot>".  The reference grammar is
		 * /^tree\s+([A-Za-z-]+)\s*$/, so anything else -- a
		 * bare "tree", a multi-word name, a digit -- is not a
		 * tree line at all and falls through to be reported as
		 * a malformed node line, exactly as it is there.
		 */
		if (word_is(body, "tree", &rest) &&
		    isspace((unsigned char)*rest) && tree_name_ok(rest)) {
			char *name = trimmed(rest);
			int slot;

			/*
			 * "left" and "right" are refused rather than
			 * mapped, because there is no mapping to make:
			 * the words name positions, and the roles they
			 * would have to name are onto and off-of.  A
			 * fixture that used them would be read by
			 * whichever mapping its reader happened to hold.
			 */
			if (strcmp(name, "left") == 0 ||
			    strcmp(name, "right") == 0) {
				add_error(sp, lineno, "tree name '%s' does "
				    "not say which side it means; write "
				    "'onto' or 'offof'", name);
				cur = -1;
				free(name);
				goto next;
			}

			slot = slot_of(name);
			if (slot < 0) {
				add_error(sp, lineno,
				    "unknown tree name '%s'", name);
				cur = -1;
			} else {
				cur = slot;
				raw[slot].rt_seen = 1;
			}
			free(name);
			goto next;
		}

		/* A node line: "<idx>-<txg>: <leaf>" with an optional
		 * " = <token>". */
		{
			const char *q = body;
			const char *dash;
			const char *colon;
			char *endp;
			uint64_t idx, txg;
			const char *eq;
			char *leaf;
			char *token = NULL;
			int forced_dir = 0;
			raw_node_t *rn;

			if (!isdigit((unsigned char)*q)) {
				add_error(sp, lineno, "not a node line "
				    "(expected '<idx>-<txg>: <leaf>')");
				goto next;
			}
			idx = strtoull(q, &endp, 10);
			dash = endp;
			if (*dash != '-' || !isdigit((unsigned char)dash[1])) {
				add_error(sp, lineno, "not a node line "
				    "(expected '<idx>-<txg>: <leaf>')");
				goto next;
			}
			txg = strtoull(dash + 1, &endp, 10);
			colon = endp;
			if (*colon != ':') {
				add_error(sp, lineno, "not a node line "
				    "(expected '<idx>-<txg>: <leaf>')");
				goto next;
			}
			if (cur < 0) {
				add_error(sp, lineno, "node line outside any "
				    "'tree base|onto|offof|expected' block");
				goto next;
			}

			rest = colon + 1;
			while (*rest != '\0' && isspace((unsigned char)*rest))
				rest++;

			eq = strchr(rest, '=');
			if (eq != NULL) {
				leaf = xstrndup(rest, (size_t)(eq - rest));
				token = trimmed(eq + 1);
			} else {
				leaf = rt_xstrdup(rest);
			}
			{
				char *t = trimmed(leaf);

				free(leaf);
				leaf = t;
			}

			{
				size_t ll = strlen(leaf);

				if (ll > 1 && leaf[ll - 1] == '/') {
					leaf[ll - 1] = '\0';
					forced_dir = 1;
				}
			}

			/*
			 * Decode LAST among the transformations and
			 * FIRST among the checks.
			 *
			 * After the '=' split, so \075 is not taken for
			 * the separator.  After the trim, so \040
			 * survives at an edge.  After the trailing-'/'
			 * marker, so \057 is not taken for it.
			 *
			 * And before the validity checks, which is the
			 * part that matters: '/' and NUL are the two
			 * bytes no file name may hold, so an escaped
			 * one has to be REJECTED rather than smuggled
			 * past a check that ran on the spelling.
			 */
			{
				char *dec = NULL;

				switch (unescape_name(leaf, &dec)) {
				case RT_UNESC_OK:
					free(leaf);
					leaf = dec;
					break;
				case RT_UNESC_BADSLASH:
					add_error(sp, lineno, "leaf '%s' has "
					    "a backslash that is not followed "
					    "by three octal digits", leaf);
					free(leaf);
					free(token);
					goto next;
				case RT_UNESC_NUL:
					add_error(sp, lineno, "leaf contains "
					    "NUL, which no file name may hold");
					free(leaf);
					free(token);
					goto next;
				default:
					add_error(sp, lineno, "leaf '%s' has "
					    "an escape above \\377, which "
					    "names no byte", leaf);
					free(leaf);
					free(token);
					goto next;
				}
			}

			if (leaf[0] == '\0') {
				add_error(sp, lineno, "empty leaf name");
				free(leaf);
				free(token);
				goto next;
			}
			if (strcmp(leaf, "/") != 0 &&
			    strchr(leaf, '/') != NULL) {
				add_error(sp, lineno, "leaf '%s' contains '/'",
				    leaf);
				free(leaf);
				free(token);
				goto next;
			}

			rn = APPEND(raw[cur].rt_nodes, raw[cur].rt_nnodes,
			    raw_node_t);
			(void) memset(rn, 0, sizeof (*rn));
			rn->rn_lineno = lineno;
			rn->rn_indent = indent;
			rn->rn_idx = idx;
			rn->rn_txg = txg;
			rn->rn_leaf = leaf;
			rn->rn_token = token;
			rn->rn_forced_dir = forced_dir;
			rn->rn_parent = -1;
		}

next:
		free(line);
		if (eol == NULL)
			break;
		p = eol + 1;
	}

	for (i = 0; i < RT_NTREES; i++) {
		sp->rts_trees[i].rtt_present = raw[i].rt_seen;
		build_tree(sp, raw[i].rt_nodes, raw[i].rt_nnodes,
		    &sp->rts_trees[i]);
	}

	/*
	 * Gold is present when it has content, not merely when the
	 * block header appeared: the reference parser builds an
	 * expected tree only from node lines, so an empty "tree
	 * expected" block carries no expectation there and must carry
	 * none here either.
	 */
	sp->rts_trees[RT_TREE_EXPECTED].rtt_present =
	    (sp->rts_trees[RT_TREE_EXPECTED].rtt_npools > 0);

	for (i = 0; i < RT_NTREES; i++) {
		for (j = 0; j < raw[i].rt_nnodes; j++) {
			free(raw[i].rt_nodes[j].rn_leaf);
			free(raw[i].rt_nodes[j].rn_token);
			free(raw[i].rt_nodes[j].rn_path);
		}
		free(raw[i].rt_nodes);
	}

	qsort(sp->rts_errors, (size_t)sp->rts_nerrors, sizeof (char *),
	    error_cmp);

	return (sp->rts_nerrors > 0 ? EINVAL : 0);
}

int
rt_spec_parse_file(const char *path, rt_spec_t *sp)
{
	FILE *f;
	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;
	int rc;

	(void) memset(sp, 0, sizeof (*sp));
	if (path == NULL)
		return (EINVAL);

	f = fopen(path, "r");
	if (f == NULL)
		return (errno != 0 ? errno : ENOENT);

	for (;;) {
		size_t got;

		if (len + 4096 + 1 > cap) {
			cap = cap * 2 + 8192;
			buf = rt_xrealloc(buf, cap);
		}
		got = fread(buf + len, 1, 4096, f);
		len += got;
		if (got < 4096)
			break;
	}
	if (buf == NULL)
		buf = rt_xmalloc(1);
	buf[len] = '\0';
	(void) fclose(f);

	rc = rt_spec_parse_text(buf, path, sp);
	free(buf);
	return (rc);
}

void
rt_spec_free(rt_spec_t *sp)
{
	int i, j, k;

	if (sp == NULL)
		return;
	free(sp->rts_path);
	free(sp->rts_title);
	for (i = 0; i < RT_NTREES; i++) {
		rt_tree_t *t = &sp->rts_trees[i];

		for (j = 0; j < t->rtt_npools; j++) {
			for (k = 0; k < t->rtt_pools[j].rtp_nnames; k++)
				free(t->rtt_pools[j].rtp_names[k]);
			free(t->rtt_pools[j].rtp_names);
			free(t->rtt_pools[j].rtp_token);
		}
		free(t->rtt_pools);
	}
	for (i = 0; i < sp->rts_nexpects; i++)
		free(sp->rts_expects[i].rte_arg);
	free(sp->rts_expects);
	for (i = 0; i < sp->rts_nerrors; i++)
		free(sp->rts_errors[i]);
	free(sp->rts_errors);
	(void) memset(sp, 0, sizeof (*sp));
}

/*
 * ------------------------------------------------------------------
 * Lookups and rendering
 * ------------------------------------------------------------------
 */

const rt_tree_pool_t *
rt_tree_by_name(const rt_tree_t *t, const char *name)
{
	int i, j;

	if (t == NULL || name == NULL)
		return (NULL);
	for (i = 0; i < t->rtt_npools; i++) {
		for (j = 0; j < t->rtt_pools[i].rtp_nnames; j++) {
			if (strcmp(t->rtt_pools[i].rtp_names[j], name) == 0)
				return (&t->rtt_pools[i]);
		}
	}
	return (NULL);
}

const rt_tree_pool_t *
rt_tree_by_key(const rt_tree_t *t, const char *key)
{
	int i;

	if (t == NULL || key == NULL)
		return (NULL);
	for (i = 0; i < t->rtt_npools; i++) {
		if (strcmp(t->rtt_pools[i].rtp_key, key) == 0)
			return (&t->rtt_pools[i]);
	}
	return (NULL);
}

int
rt_tree_nnames(const rt_tree_t *t)
{
	int n = 0;
	int i;

	if (t == NULL)
		return (0);
	for (i = 0; i < t->rtt_npools; i++)
		n += t->rtt_pools[i].rtp_nnames;
	return (n);
}

/*
 * Names across the three INPUT trees, counted once each.  Within one
 * tree a name is unique already (the parser rejects two pools holding
 * one name), so de-duplication only has to look at earlier trees.
 * The expected tree is gold rather than input and takes no part.
 */
int
rt_spec_union_names(const rt_spec_t *sp)
{
	int n = 0;
	int t, i, j, u;

	for (t = RT_TREE_BASE; t <= RT_TREE_OFFOF; t++) {
		const rt_tree_t *tr = &sp->rts_trees[t];

		for (i = 0; i < tr->rtt_npools; i++) {
			for (j = 0; j < tr->rtt_pools[i].rtp_nnames; j++) {
				const char *nm = tr->rtt_pools[i].rtp_names[j];
				int seen = 0;

				for (u = RT_TREE_BASE; u < t; u++) {
					if (rt_tree_by_name(&sp->rts_trees[u],
					    nm) != NULL) {
						seen = 1;
						break;
					}
				}
				if (!seen)
					n++;
			}
		}
	}
	return (n);
}

const char *
rt_tree_slot_name(int slot)
{
	switch (slot) {
	case RT_TREE_BASE:
		return ("base");
	case RT_TREE_ONTO:
		return ("onto");
	case RT_TREE_OFFOF:
		return ("off-of");
	case RT_TREE_EXPECTED:
		return ("expected");
	default:
		return ("?");
	}
}

/* The dump slot names are the file's own spelling, not the prose
 * one: "offof", so a reader can paste a line back into a fixture. */
static const char *
dump_slot_name(int slot)
{
	return (slot == RT_TREE_OFFOF ? "offof" : rt_tree_slot_name(slot));
}

void
rt_spec_dump(const rt_spec_t *sp, FILE *out)
{
	int i, j, k;

	(void) fprintf(out, "title %s\n", sp->rts_title);

	for (i = 0; i < RT_NTREES; i++) {
		const rt_tree_t *t = &sp->rts_trees[i];

		if (i == RT_TREE_EXPECTED && t->rtt_npools == 0)
			continue;
		(void) fprintf(out, "tree %s\n", dump_slot_name(i));
		for (j = 0; j < t->rtt_npools; j++) {
			const rt_tree_pool_t *pool = &t->rtt_pools[j];

			(void) fprintf(out, "  pool %s %s token=",
			    pool->rtp_key, pool->rtp_isdir ? "dir" : "file");
			fput_escaped(out, pool->rtp_token);
			(void) fputc('\n', out);
			for (k = 0; k < pool->rtp_nnames; k++) {
				(void) fprintf(out, "    name ");
				fput_escaped(out, pool->rtp_names[k]);
				(void) fputc('\n', out);
			}
		}
	}

	for (i = 0; i < sp->rts_nexpects; i++) {
		const rt_expect_t *ex = &sp->rts_expects[i];

		switch (ex->rte_kind) {
		case RT_EXP_CLEAN:
			(void) fprintf(out, "expect clean\n");
			break;
		case RT_EXP_CONFLICT:
			(void) fprintf(out, "expect conflict %s\n",
			    ex->rte_arg);
			break;
		case RT_EXP_QUARANTINED:
			(void) fprintf(out, "expect quarantined %s\n",
			    ex->rte_arg);
			break;
		default:
			break;
		}
	}

	for (i = 0; i < sp->rts_nerrors; i++)
		(void) fprintf(out, "error %s\n", sp->rts_errors[i]);
}
