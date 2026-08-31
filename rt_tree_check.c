// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Compare a fixture's gold against what the engine decided.
 *
 * No ZFS here: this works on the flat rt_dview_t, which is why
 * devcheck/checkcheck.c can exercise it on a machine with no pool.
 *
 * WHAT THE EXPECTED TREE ASSERTS, and in what order it is checked:
 *
 *   1. SURVIVAL, exhaustively.  A name the expected tree lists must
 *      survive; a name it omits must not.  There is no "absent"
 *      directive because the tree already says so, and that is the
 *      strongest thing about the format -- a fixture cannot quietly
 *      forget to assert a deletion.
 *   2. POOLING.  Names sharing one key must land in ONE output pool;
 *      names with different keys must not share one.
 *   3. CONTENT and TYPE, from the pool's token and its shape.
 *   4. REALIZATION.  A key onto also uses asserts the output reuses
 *      that dnode; a key onto does not have asserts a fresh one.
 *
 * QUARANTINE IS EXEMPT, and this is not a convenience.  A quarantined
 * component keeps onto's arrangement untouched, so it has no output
 * pool at all -- there is nothing there to assert pooling, content or
 * realization about, and the decision's own view of those names is
 * not what emerges.  Asserting them anyway is a modeling error that
 * produces confident nonsense; the corpus caught exactly that
 * mistake, in the four examples that have conflicts.  What a
 * quarantined region CAN be asserted about is that it was held back,
 * which is what "expect quarantined" is for.
 */

#include "rt_tree_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void
rt_check(rt_check_result_t *res, int ok, const char *fmt, ...)
{
	va_list ap;

	res->rcr_checks++;
	if (ok)
		return;

	res->rcr_failures++;
	if (res->rcr_first[0] != '\0')
		return;

	va_start(ap, fmt);
	(void) vsnprintf(res->rcr_first, sizeof (res->rcr_first), fmt, ap);
	va_end(ap);
}

static const struct {
	const char	*ck_name;
	int		ck_bit;
} rt_kinds[] = {
	{ "lineage",	RT_CK_LINEAGE },
	{ "name",	RT_CK_NAME },
	{ "pooling",	RT_CK_POOLING },
	{ "content",	RT_CK_CONTENT },
	{ "structural",	RT_CK_STRUCTURAL }
};

#define	RT_NKINDS	(int)(sizeof (rt_kinds) / sizeof (rt_kinds[0]))

int
rt_conflict_kind_bit(const char *name)
{
	int i;

	for (i = 0; i < RT_NKINDS; i++) {
		if (strcmp(rt_kinds[i].ck_name, name) == 0)
			return (rt_kinds[i].ck_bit);
	}
	return (0);
}

const char *
rt_conflict_kind_name(int bit)
{
	int i;

	for (i = 0; i < RT_NKINDS; i++) {
		if (rt_kinds[i].ck_bit == bit)
			return (rt_kinds[i].ck_name);
	}
	return ("?");
}

static const rt_dv_name_t *
dv_find(const rt_dview_t *dv, const char *name)
{
	int i;

	for (i = 0; i < dv->dv_nnames; i++) {
		if (strcmp(dv->dv_names[i].dvn_name, name) == 0)
			return (&dv->dv_names[i]);
	}
	return (NULL);
}

/* Is this name listed anywhere in the expected tree? */
static int
gold_lists(const rt_tree_t *exp, const char *name)
{
	return (rt_tree_by_name(exp, name) != NULL);
}

/*
 * A pool whose every name was held back is exempt from the pooling,
 * content and realization checks: there is no output pool to compare
 * against.  A pool only PARTLY quarantined would be incoherent -- the
 * quarantine works on whole components -- so it is reported rather
 * than quietly averaged.
 */
static int
gold_pool_quarantined(const rt_dview_t *dv, const rt_tree_pool_t *pool,
    int *mixedp)
{
	int nq = 0;
	int j;

	for (j = 0; j < pool->rtp_nnames; j++) {
		const rt_dv_name_t *n = dv_find(dv, pool->rtp_names[j]);

		if (n != NULL && n->dvn_quarantined)
			nq++;
	}
	*mixedp = (nq != 0 && nq != pool->rtp_nnames);
	return (nq == pool->rtp_nnames && nq != 0);
}

void
rt_tree_check_view(const rt_spec_t *sp, const rt_dview_t *dv,
    rt_check_result_t *res)
{
	const rt_tree_t *exp = &sp->rts_trees[RT_TREE_EXPECTED];
	const rt_tree_t *onto = &sp->rts_trees[RT_TREE_ONTO];
	int i, j;

	/*
	 * --- The expect directives, which say what a tree cannot ---
	 */
	for (i = 0; i < sp->rts_nexpects; i++) {
		const rt_expect_t *ex = &sp->rts_expects[i];
		const rt_dv_name_t *n;
		int bit;

		switch (ex->rte_kind) {
		case RT_EXP_CLEAN:
			rt_check(res, dv->dv_nconflicts == 0,
			    "expected a clean run, got %d conflict(s)",
			    dv->dv_nconflicts);
			break;
		case RT_EXP_CONFLICT:
			bit = rt_conflict_kind_bit(ex->rte_arg);
			if (bit == 0) {
				rt_check(res, 0, "line %d: '%s' is not a "
				    "conflict kind", ex->rte_line,
				    ex->rte_arg);
				break;
			}
			rt_check(res, (dv->dv_conflict_kinds & bit) != 0,
			    "expected a %s conflict, none reported",
			    ex->rte_arg);
			break;
		case RT_EXP_QUARANTINED:
			n = dv_find(dv, ex->rte_arg);
			if (n == NULL) {
				rt_check(res, 0, "expected %s to be "
				    "quarantined, but no such name exists",
				    ex->rte_arg);
				break;
			}
			rt_check(res, n->dvn_quarantined,
			    "expected %s to be quarantined, it was not",
			    ex->rte_arg);
			break;
		default:
			break;
		}
	}

	if (exp->rtt_npools == 0)
		return;			/* no gold tree; directives only */

	/*
	 * --- 1. Survival, exhaustively ---
	 */
	for (i = 0; i < exp->rtt_npools; i++) {
		const rt_tree_pool_t *pool = &exp->rtt_pools[i];

		for (j = 0; j < pool->rtp_nnames; j++) {
			const rt_dv_name_t *n = dv_find(dv,
			    pool->rtp_names[j]);

			if (n == NULL) {
				rt_check(res, 0, "%s should survive, but the "
				    "run never saw that name",
				    pool->rtp_names[j]);
				continue;
			}
			if (n->dvn_quarantined)
				continue;
			rt_check(res, n->dvn_survives,
			    "%s should survive, it did not",
			    pool->rtp_names[j]);
		}
	}

	for (i = 0; i < dv->dv_nnames; i++) {
		const rt_dv_name_t *n = &dv->dv_names[i];

		if (n->dvn_quarantined || !n->dvn_survives)
			continue;
		rt_check(res, gold_lists(exp, n->dvn_name),
		    "%s survived, but the expected tree does not list it",
		    n->dvn_name);
	}

	/*
	 * --- 2. Pooling ---
	 *
	 * Checked after presence so a hard link that lost one of its
	 * names reports as the missing name, not as a split pool.
	 */
	for (i = 0; i < exp->rtt_npools; i++) {
		const rt_tree_pool_t *pool = &exp->rtt_pools[i];
		int mixed;
		int first = RT_NO_POOL;

		if (gold_pool_quarantined(dv, pool, &mixed))
			continue;
		if (mixed) {
			rt_check(res, 0, "pool %s has some names quarantined "
			    "and some not; quarantine works on whole "
			    "components", pool->rtp_key);
			continue;
		}

		for (j = 0; j < pool->rtp_nnames; j++) {
			const rt_dv_name_t *n = dv_find(dv,
			    pool->rtp_names[j]);

			if (n == NULL || !n->dvn_survives)
				continue;	/* already reported above */
			if (n->dvn_outpool == RT_NO_POOL) {
				rt_check(res, 0, "%s survived but landed in "
				    "no output pool", pool->rtp_names[j]);
				continue;
			}
			if (first == RT_NO_POOL) {
				first = n->dvn_outpool;
				continue;
			}
			rt_check(res, n->dvn_outpool == first,
			    "%s and %s share identity %s but landed in "
			    "different output pools", pool->rtp_names[0],
			    pool->rtp_names[j], pool->rtp_key);
		}
	}

	/* Different identities must not be merged into one pool. */
	for (i = 0; i < exp->rtt_npools; i++) {
		const rt_tree_pool_t *a = &exp->rtt_pools[i];
		const rt_dv_name_t *na;
		int mixed;

		if (gold_pool_quarantined(dv, a, &mixed) || mixed)
			continue;
		na = dv_find(dv, a->rtp_names[0]);
		if (na == NULL || na->dvn_outpool == RT_NO_POOL)
			continue;

		for (j = i + 1; j < exp->rtt_npools; j++) {
			const rt_tree_pool_t *b = &exp->rtt_pools[j];
			const rt_dv_name_t *nb;
			int bmixed;

			if (gold_pool_quarantined(dv, b, &bmixed) || bmixed)
				continue;
			nb = dv_find(dv, b->rtp_names[0]);
			if (nb == NULL || nb->dvn_outpool == RT_NO_POOL)
				continue;
			rt_check(res, na->dvn_outpool != nb->dvn_outpool,
			    "%s and %s are different identities (%s, %s) but "
			    "landed in one output pool", a->rtp_names[0],
			    b->rtp_names[0], a->rtp_key, b->rtp_key);
		}
	}

	/*
	 * --- 3. Type and content, and 4. Realization ---
	 */
	for (i = 0; i < exp->rtt_npools; i++) {
		const rt_tree_pool_t *pool = &exp->rtt_pools[i];
		const rt_dv_name_t *n;
		const rt_dv_pool_t *op;
		int mixed;
		int in_onto;

		if (gold_pool_quarantined(dv, pool, &mixed) || mixed)
			continue;
		n = dv_find(dv, pool->rtp_names[0]);
		if (n == NULL || !n->dvn_survives ||
		    n->dvn_outpool == RT_NO_POOL)
			continue;		/* already reported */
		if (n->dvn_outpool < 0 || n->dvn_outpool >= dv->dv_npools) {
			rt_check(res, 0, "%s names output pool %d, which does "
			    "not exist", pool->rtp_names[0], n->dvn_outpool);
			continue;
		}
		op = &dv->dv_pools[n->dvn_outpool];

		rt_check(res, op->dvp_isdir == pool->rtp_isdir,
		    "%s should be a %s, the run made it a %s",
		    pool->rtp_names[0], pool->rtp_isdir ? "directory" : "file",
		    op->dvp_isdir ? "directory" : "file");

		/*
		 * Directory content is its attributes, which a fixture
		 * has no way to set, so only files carry a meaningful
		 * token to compare.
		 */
		if (!pool->rtp_isdir) {
			const char *got = op->dvp_content != NULL ?
			    op->dvp_content : "";

			rt_check(res, strcmp(got, pool->rtp_token) == 0,
			    "%s should hold '%s', the run chose '%s'",
			    pool->rtp_names[0], pool->rtp_token, got);
		}

		/*
		 * Realization.  A key onto also uses means the output
		 * must reuse that dnode -- the substrate is not
		 * rewritten where it already agrees.  A key onto does
		 * not have has to be made fresh.
		 */
		in_onto = (rt_tree_by_key(onto, pool->rtp_key) != NULL);
		rt_check(res, op->dvp_materialized == !in_onto,
		    "%s (%s) should have been %s, it was %s",
		    pool->rtp_names[0], pool->rtp_key,
		    in_onto ? "taken from onto's dnode" : "materialized fresh",
		    op->dvp_materialized ? "materialized fresh" :
		    "taken from onto's dnode");
	}
}
