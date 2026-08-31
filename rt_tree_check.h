// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Comparing gold to a decision -- and a flat view of a decision, so
 * that the comparing can be tested without one.
 *
 * rebase_decision_t (sys/dsl_rebase.h) is the real article: name
 * table, components, CSR slices, output pools, conflicts, quarantine.
 * Reading it needs the ZFS headers, a pool, and a box.  But almost
 * nothing this checker does is about ZFS: it is about whether a set
 * of names survived, which of them landed in one pool, what bytes
 * that pool holds, and whether the dnode was reused or made fresh.
 *
 * So the comparison is written against rt_dview_t, a flat rendering
 * of exactly those facts, and rt_decision.c is the thin adapter that
 * fills one in from the real record.  The payoff is that
 * devcheck/checkcheck.c can build views by hand on a laptop and prove
 * the checker COMPLAINS when it should -- which is the only property
 * of a checker anyone should believe without evidence, and the one
 * nobody ever checks.
 */

#ifndef	_RT_TREE_CHECK_H
#define	_RT_TREE_CHECK_H

#include "rt_tree.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* Conflict kinds as bits, mirroring rebase_conflict_kind_t. */
#define	RT_CK_LINEAGE		0x01
#define	RT_CK_NAME		0x02
#define	RT_CK_POOLING		0x04
#define	RT_CK_CONTENT		0x08
#define	RT_CK_STRUCTURAL	0x10

#define	RT_NO_POOL		(-1)

typedef struct rt_dv_name {
	const char	*dvn_name;
	int		dvn_survives;
	/*
	 * In a component the quarantine held back.  Such a name keeps
	 * onto's arrangement verbatim, so the decision's own view of
	 * it is not what emerges and must not be asserted against.
	 */
	int		dvn_quarantined;
	int		dvn_outpool;	/* index, or RT_NO_POOL */
} rt_dv_name_t;

typedef struct rt_dv_pool {
	int		dvp_isdir;
	int		dvp_materialized;	/* 0 = an onto dnode reused */
	const char	*dvp_content;		/* chosen bytes, as a token */
} rt_dv_pool_t;

typedef struct rt_dview {
	rt_dv_name_t	*dv_names;	/* every name the run knows */
	int		dv_nnames;
	rt_dv_pool_t	*dv_pools;
	int		dv_npools;
	int		dv_conflict_kinds;	/* RT_CK_* bits */
	int		dv_nconflicts;
	int		dv_nquarantined;	/* components held back */
	/*
	 * Backing store the view owns, when something built it by
	 * copying (the adapter has to, since content is read off
	 * disk).  A view assembled by hand leaves this NULL and owns
	 * nothing.  The checker never looks at it.
	 */
	void		*dv_owned;
} rt_dview_t;

typedef struct rt_check_result {
	int	rcr_checks;
	int	rcr_failures;
	char	rcr_first[256];		/* first complaint, for summaries */
} rt_check_result_t;

/* Record one check. msg is rendered only when ok is false. */
void rt_check(rt_check_result_t *res, int ok, const char *fmt, ...);

/*
 * Compare a fixture's gold against a decision view.
 *
 * Order matters and is contract: presence, then pooling, then
 * content, then realization.  A split hard link checked
 * content-first reports as a content mismatch and sends the reader
 * looking in the wrong place; checked presence-first it reports as
 * what it is.
 */
void rt_tree_check_view(const rt_spec_t *sp, const rt_dview_t *dv,
    rt_check_result_t *res);

int rt_conflict_kind_bit(const char *name);
const char *rt_conflict_kind_name(int bit);

#ifdef	__cplusplus
}
#endif

#endif	/* _RT_TREE_CHECK_H */
