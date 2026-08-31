// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Getting the decision record out of a run, and flattening it.
 *
 * This is the only file in the suite that reads rebase_decision_t,
 * and it is deliberately the thinnest thing that can do the job: the
 * judgement lives in rt_tree_check.c, where it can be tested without
 * a pool.  Everything here is mechanical.
 *
 * THE ACCESSOR.  sys/dsl_rebase.h already says of rebase_decision_t:
 * "the test interface: the harness asserts against this structure,
 * never against scraped debug text."  Every field below is already
 * declared there.  What does not exist is any way to obtain one --
 * dsl_rebase() builds the record, stops at the emit boundary, returns
 * ENOSYS and releases the arena, so the record is gone before the
 * caller sees it.  The contract for the two functions that would fix
 * that is written out in rt_tree_suite.h.
 *
 * Until it lands, RT_HAVE_DECIDE_ACCESSOR is undefined, this returns
 * ENOTSUP, and the suite falls back to its census tier and says so on
 * every line.  When it lands, define RT_HAVE_DECIDE_ACCESSOR and
 * nothing else in the suite changes.
 */

#include "rt_tree_suite.h"

#ifdef	RT_HAVE_DECIDE_ACCESSOR
typedef int (*rebase_inspect_cb_t)(const rebase_run_t *rr, void *arg);
extern int dsl_rebase_inspect(const char *offof_snap,
    const char *onto_snap, rebase_inspect_cb_t cb, void *arg);
#endif

/*
 * The single call site.  Base is not passed: it is the common
 * ancestor of the two snapshot chains and the engine discovers it,
 * which means every fixture in the corpus exercises that discovery
 * as a side effect of doing anything at all.
 */
int
rt_engine_run(nvlist_t *outnvl)
{
	return (dsl_rebase(RT_SNAP_OFFOF, RT_SNAP_ONTO, outnvl));
}

int
rt_decision_available(void)
{
#ifdef	RT_HAVE_DECIDE_ACCESSOR
	return (1);
#else
	return (0);
#endif
}

/*
 * The comparison runs INSIDE the engine's callback, where the arena
 * is still alive.  Everything the view points at -- the name strings
 * above all -- belongs to that arena, so carrying the view back out
 * would be carrying dangling pointers out.  Doing the work here
 * costs nothing and removes the question entirely.
 */
typedef struct inspect_ctx {
	const rt_spec_t		*ic_spec;
	rt_check_result_t	*ic_res;
	char			*ic_err;
	size_t			ic_errlen;
	int			ic_rc;
} inspect_ctx_t;

#ifdef	RT_HAVE_DECIDE_ACCESSOR
static int
rt_inspect_cb(const rebase_run_t *rr, void *arg)
{
	inspect_ctx_t *ctx = arg;
	rt_dview_t dv;

	ctx->ic_rc = rt_decision_to_view(&rr->rr_decision, &dv, ctx->ic_err,
	    ctx->ic_errlen);
	if (ctx->ic_rc != 0)
		return (ctx->ic_rc);

	rt_tree_check_view(ctx->ic_spec, &dv, ctx->ic_res);
	rt_dview_free(&dv);
	return (0);
}
#endif

int
rt_decision_check(const rt_spec_t *sp, rt_check_result_t *res, char *errbuf,
    size_t errlen)
{
#ifdef	RT_HAVE_DECIDE_ACCESSOR
	inspect_ctx_t ctx;
	int err;

	ctx.ic_spec = sp;
	ctx.ic_res = res;
	ctx.ic_err = errbuf;
	ctx.ic_errlen = errlen;
	ctx.ic_rc = 0;

	/*
	 * A nonzero return means the engine could NOT decide.  A
	 * conflicted run decides and returns zero, with the conflicts
	 * in the record where the fixture expects to find them.
	 */
	err = dsl_rebase_inspect(RT_SNAP_OFFOF, RT_SNAP_ONTO,
	    rt_inspect_cb, &ctx);
	if (err != 0) {
		(void) snprintf(errbuf, errlen, "engine could not decide: "
		    "%s (%d)", strerror(err), err);
		return (err);
	}
	return (ctx.ic_rc);
#else
	(void) sp;
	(void) res;
	(void) snprintf(errbuf, errlen, "built without the inspection seam");
	return (ENOTSUP);
#endif
}

/*
 * ------------------------------------------------------------------
 * Flattening
 * ------------------------------------------------------------------
 */

/* Content tokens in fixtures are short; this is slack, not a limit. */
#define	RT_TOKEN_MAX	256

/*
 * Survival is read from the per-component survivor slices rather than
 * from rd_survives.  Both say the same thing, but the CSR's layout is
 * spelled out in the header (Definition 11.1) while the bitmap's bit
 * order is not, and a test harness should not be guessing at the bit
 * order of the thing it is testing.
 */
static void
mark_survivors(const rebase_decision_t *rd, rt_dv_name_t *names)
{
	uint32_t c, k;

	for (c = 0; c < rd->rd_ncomponents; c++) {
		const rebase_component_t *cm = &rd->rd_components[c];

		for (k = 0; k < cm->rcm_nsurvivors; k++) {
			rebase_name_id_t n =
			    rd->rd_survivor_csr[cm->rcm_first_survivor + k];

			if (n < rd->rd_names->rnt_count)
				names[n].dvn_survives = 1;
		}
	}
}

/*
 * Every name of every held-back component.  The quarantine names
 * components, not names, because holding back half a component would
 * leave the output referring to a decision that was never emitted.
 */
static void
mark_quarantined(const rebase_decision_t *rd, rt_dv_name_t *names)
{
	uint32_t q, c, k;

	for (q = 0; q < rd->rd_nquarantine; q++) {
		uint32_t want = rd->rd_quarantine[q].rq_component;

		for (c = 0; c < rd->rd_ncomponents; c++) {
			const rebase_component_t *cm = &rd->rd_components[c];

			if (cm->rcm_id != want)
				continue;
			for (k = 0; k < cm->rcm_nnames; k++) {
				rebase_name_id_t n =
				    rd->rd_name_csr[cm->rcm_first_name + k];

				if (n < rd->rd_names->rnt_count)
					names[n].dvn_quarantined = 1;
			}
			break;
		}
	}
}

/*
 * The bytes an output pool decided on.  The decision does not carry
 * content; it carries a CHOICE -- take the bytes of this pool of this
 * tree -- so reading them back means going to that tree's objset.
 * That is the honest thing to compare against a fixture token: it is
 * what the emit phase would go on to copy.
 */
static void
read_choice(const rebase_decision_t *rd, const rebase_outpool_t *op,
    char *buf, size_t buflen)
{
	const rebase_tree_t *tree;
	const rebase_pool_t *pool;
	uint64_t len;
	int tidx;

	buf[0] = '\0';

	if (op->rop_taken_tree == REBASE_TREE_UNSET ||
	    op->rop_taken_from == REBASE_NO_POOL)
		return;

	tidx = REBASE_TIDX(op->rop_taken_tree);
	if (tidx < 0 || tidx >= REBASE_TREES)
		return;
	tree = &rd->rd_trees[tidx];
	if (op->rop_taken_from >= tree->rt_npools)
		return;

	pool = &tree->rt_pools[op->rop_taken_from];
	if (pool->rp_type == REBASE_NTYPE_DIR)
		return;

	len = pool->rp_content.rc_size;
	if (len >= buflen)
		len = buflen - 1;
	if (len == 0)
		return;

	if (rt_read_data(tree->rt_os, pool->rp_id.rpi_index, 0, len,
	    buf) != 0) {
		(void) snprintf(buf, buflen, "<unreadable>");
		return;
	}
	buf[len] = '\0';
}

static int
kind_bit(rebase_conflict_kind_t kind)
{
	switch (kind) {
	case REBASE_CONFLICT_LINEAGE:
		return (RT_CK_LINEAGE);
	case REBASE_CONFLICT_NAME:
		return (RT_CK_NAME);
	case REBASE_CONFLICT_POOLING:
		return (RT_CK_POOLING);
	case REBASE_CONFLICT_CONTENT:
		return (RT_CK_CONTENT);
	case REBASE_CONFLICT_STRUCTURAL:
		return (RT_CK_STRUCTURAL);
	default:
		return (0);
	}
}

int
rt_decision_to_view(const rebase_decision_t *rd, rt_dview_t *dv,
    char *errbuf, size_t errlen)
{
	uint32_t nn, np;
	uint32_t i, k;
	char *tokens = NULL;

	(void) memset(dv, 0, sizeof (*dv));

	if (rd == NULL || rd->rd_names == NULL) {
		(void) snprintf(errbuf, errlen, "no decision record");
		return (EINVAL);
	}

	nn = rd->rd_names->rnt_count;
	np = rd->rd_noutpools;

	dv->dv_names = rt_xmalloc((size_t)(nn + 1) * sizeof (rt_dv_name_t));
	(void) memset(dv->dv_names, 0, (size_t)(nn + 1) *
	    sizeof (rt_dv_name_t));
	dv->dv_nnames = (int)nn;

	dv->dv_pools = rt_xmalloc((size_t)(np + 1) * sizeof (rt_dv_pool_t));
	(void) memset(dv->dv_pools, 0, (size_t)(np + 1) *
	    sizeof (rt_dv_pool_t));
	dv->dv_npools = (int)np;

	tokens = rt_xmalloc((size_t)(np + 1) * RT_TOKEN_MAX);
	(void) memset(tokens, 0, (size_t)(np + 1) * RT_TOKEN_MAX);
	dv->dv_owned = tokens;

	for (i = 0; i < nn; i++) {
		dv->dv_names[i].dvn_name = rd->rd_names->rnt_names[i];
		dv->dv_names[i].dvn_outpool = RT_NO_POOL;
	}

	mark_survivors(rd, dv->dv_names);
	mark_quarantined(rd, dv->dv_names);

	for (i = 0; i < np; i++) {
		const rebase_outpool_t *op = &rd->rd_outpools[i];
		char *tok = tokens + (size_t)i * RT_TOKEN_MAX;

		dv->dv_pools[i].dvp_isdir =
		    (op->rop_type == REBASE_NTYPE_DIR);
		dv->dv_pools[i].dvp_materialized =
		    (op->rop_realization == REBASE_REAL_MATERIALIZED);

		read_choice(rd, op, tok, RT_TOKEN_MAX);
		dv->dv_pools[i].dvp_content = tok;

		for (k = 0; k < op->rop_nnames; k++) {
			rebase_name_id_t n = op->rop_names[k];

			if (n < nn)
				dv->dv_names[n].dvn_outpool = (int)i;
		}
	}

	for (i = 0; i < rd->rd_nconflicts; i++)
		dv->dv_conflict_kinds |= kind_bit(rd->rd_conflicts[i].rcf_kind);
	dv->dv_nconflicts = (int)rd->rd_nconflicts;
	dv->dv_nquarantined = (int)rd->rd_nquarantine;
	return (0);
}

void
rt_dview_free(rt_dview_t *dv)
{
	free(dv->dv_owned);
	free(dv->dv_names);
	free(dv->dv_pools);
	(void) memset(dv, 0, sizeof (*dv));
}
