// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * checkcheck -- prove the gold checker COMPLAINS when it should.
 *
 * A checker that only ever passes is worthless, and a checker is the
 * one piece of test machinery nobody tests: it is used to judge
 * everything else, so when it is too permissive every fixture goes
 * green and the suite reports health it never measured.
 *
 * So this builds an agreeing decision view by hand, confirms silence,
 * then perturbs it one way at a time -- wrong content, split pooling,
 * merged identities, a survivor gold does not list, a name gold wants
 * that did not survive, a clean verdict that was not clean, a dnode
 * reused that should have been made fresh -- and requires a complaint
 * each time.  It also pins the ORDER contract: a hard link that lost
 * a name must report as the missing name, not as a split pool, or the
 * reader goes looking in the wrong place.
 *
 * No ZFS, no pool, no box: the checker works on rt_dview_t precisely
 * so this can run where the code is written.
 *
 *   cc -o checkcheck devcheck/checkcheck.c rt_tree_check.c \
 *       rt_tree_parse.c -I.
 */

#include "../rt_tree_check.h"

#include <stdio.h>
#include <string.h>

static int cc_cases;
static int cc_failures;

/*
 * One scenario: a hard-linked file under a directory, edited on the
 * off-of side, everything else quiet.  Small enough to reason about
 * in full, and it still carries a hard link, a directory, a root and
 * an edit, which is every shape the checker treats differently.
 */
static const char *FIXTURE =
	"tree base\n"
	"  1-1: /\n"
	"    2-100: dir/\n"
	"      10-100: a = v1\n"
	"      10-100: b\n"
	"tree onto\n"
	"  1-1: /\n"
	"    2-100: dir/\n"
	"      10-100: a = v1\n"
	"      10-100: b\n"
	"tree offof\n"
	"  1-1: /\n"
	"    2-100: dir/\n"
	"      10-100: a = v2\n"
	"      10-100: b\n"
	"expect clean\n"
	"tree expected\n"
	"  1-1: /\n"
	"    2-100: dir/\n"
	"      10-100: a = v2\n"
	"      10-100: b\n";

#define	MAXN	8
#define	MAXP	8

typedef struct scene {
	rt_dview_t	sc_dv;
	rt_dv_name_t	sc_names[MAXN];
	rt_dv_pool_t	sc_pools[MAXP];
} scene_t;

/*
 * The view the engine ought to produce for FIXTURE: three output
 * pools, the hard link's two names sharing the third, nothing
 * materialized because onto already holds every identity.
 */
static void
scene_init(scene_t *s)
{
	(void) memset(s, 0, sizeof (*s));

	s->sc_names[0].dvn_name = "/";
	s->sc_names[0].dvn_outpool = 0;
	s->sc_names[1].dvn_name = "/dir";
	s->sc_names[1].dvn_outpool = 1;
	s->sc_names[2].dvn_name = "/dir/a";
	s->sc_names[2].dvn_outpool = 2;
	s->sc_names[3].dvn_name = "/dir/b";
	s->sc_names[3].dvn_outpool = 2;
	s->sc_names[0].dvn_survives = 1;
	s->sc_names[1].dvn_survives = 1;
	s->sc_names[2].dvn_survives = 1;
	s->sc_names[3].dvn_survives = 1;

	s->sc_pools[0].dvp_isdir = 1;
	s->sc_pools[0].dvp_content = "";
	s->sc_pools[1].dvp_isdir = 1;
	s->sc_pools[1].dvp_content = "";
	s->sc_pools[2].dvp_isdir = 0;
	s->sc_pools[2].dvp_content = "v2";

	s->sc_dv.dv_names = s->sc_names;
	s->sc_dv.dv_nnames = 4;
	s->sc_dv.dv_pools = s->sc_pools;
	s->sc_dv.dv_npools = 3;
}

static int
name_at(scene_t *s, const char *name)
{
	int i;

	for (i = 0; i < s->sc_dv.dv_nnames; i++) {
		if (strcmp(s->sc_names[i].dvn_name, name) == 0)
			return (i);
	}
	(void) fprintf(stderr, "checkcheck: no name %s in scene\n", name);
	return (0);
}

/*
 * Run the checker over a perturbed scene.  want_fail is how many
 * complaints the perturbation should draw; want_first is a fragment
 * the FIRST complaint must contain, which is how the ordering
 * contract gets pinned rather than merely described.
 */
static void
expect(const rt_spec_t *sp, scene_t *s, const char *what, int want_fail,
    const char *want_first)
{
	rt_check_result_t res;

	(void) memset(&res, 0, sizeof (res));
	rt_tree_check_view(sp, &s->sc_dv, &res);

	cc_cases++;

	if (want_fail == 0) {
		if (res.rcr_failures == 0) {
			(void) printf("PASS  %s (%d checks, silent)\n", what,
			    res.rcr_checks);
			return;
		}
		(void) printf("FAIL  %s: expected silence, got %d "
		    "complaint(s), first: %s\n", what, res.rcr_failures,
		    res.rcr_first);
		cc_failures++;
		return;
	}

	if (res.rcr_failures == 0) {
		(void) printf("FAIL  %s: expected a complaint, checker was "
		    "silent over %d checks\n", what, res.rcr_checks);
		cc_failures++;
		return;
	}
	if (want_first != NULL && strstr(res.rcr_first, want_first) == NULL) {
		(void) printf("FAIL  %s: first complaint should mention "
		    "'%s'\n      got: %s\n", what, want_first, res.rcr_first);
		cc_failures++;
		return;
	}
	(void) printf("PASS  %s (complains: %s)\n", what, res.rcr_first);
}

int
main(void)
{
	rt_spec_t spec;
	scene_t s;
	int rc;

	rc = rt_spec_parse_text(FIXTURE, "checkcheck", &spec);
	if (rc != 0) {
		(void) printf("FAIL  the built-in fixture does not parse:\n");
		if (spec.rts_nerrors > 0)
			(void) printf("      %s\n", spec.rts_errors[0]);
		return (1);
	}

	/* The agreeing case must be silent, or nothing below means much. */
	scene_init(&s);
	expect(&spec, &s, "an agreeing decision draws no complaint", 0, NULL);

	scene_init(&s);
	s.sc_pools[2].dvp_content = "v1";
	expect(&spec, &s, "the losing side's bytes are caught", 1,
	    "should hold 'v2'");

	/*
	 * A split hard link: both names survive, but in two pools.
	 * This is the case the whole format exists to catch, because
	 * nothing about the two names individually looks wrong.
	 */
	scene_init(&s);
	s.sc_names[name_at(&s, "/dir/b")].dvn_outpool = 3;
	s.sc_pools[3].dvp_isdir = 0;
	s.sc_pools[3].dvp_content = "v2";
	s.sc_dv.dv_npools = 4;
	expect(&spec, &s, "a split hard link is caught", 1,
	    "different output pools");

	scene_init(&s);
	s.sc_names[name_at(&s, "/dir")].dvn_outpool = 2;
	expect(&spec, &s, "two identities merged into one pool is caught", 1,
	    "one output pool");

	scene_init(&s);
	s.sc_names[4].dvn_name = "/dir/c";
	s.sc_names[4].dvn_survives = 1;
	s.sc_names[4].dvn_outpool = 2;
	s.sc_dv.dv_nnames = 5;
	expect(&spec, &s, "a survivor gold does not list is caught", 1,
	    "does not list it");

	/*
	 * The ordering contract: a hard link that lost a name has both
	 * a missing survivor AND (vacuously) a pooling story.  The
	 * reader must be told about the missing name.
	 */
	scene_init(&s);
	s.sc_names[name_at(&s, "/dir/b")].dvn_survives = 0;
	s.sc_names[name_at(&s, "/dir/b")].dvn_outpool = RT_NO_POOL;
	expect(&spec, &s, "a lost name reports as lost, not as pooling", 1,
	    "/dir/b should survive");

	scene_init(&s);
	s.sc_dv.dv_nconflicts = 1;
	s.sc_dv.dv_conflict_kinds = RT_CK_CONTENT;
	expect(&spec, &s, "a run that was not clean is caught", 1,
	    "expected a clean run");

	/*
	 * Realization.  Onto already holds identity 10-100, so the
	 * output must reuse that dnode rather than build a new one --
	 * rewriting the substrate where it already agrees is exactly
	 * the waste the rule exists to forbid.
	 */
	scene_init(&s);
	s.sc_pools[2].dvp_materialized = 1;
	expect(&spec, &s, "a needlessly materialized dnode is caught", 1,
	    "taken from onto's dnode");

	/*
	 * Quarantine exemption.  A held-back component keeps onto's
	 * arrangement, so it has no output pool to be judged against.
	 * Perturb everything about it and the checker must still be
	 * silent -- if it is not, every conflicted fixture in the
	 * corpus will report confident nonsense.
	 */
	scene_init(&s);
	s.sc_names[name_at(&s, "/dir/a")].dvn_quarantined = 1;
	s.sc_names[name_at(&s, "/dir/b")].dvn_quarantined = 1;
	s.sc_names[name_at(&s, "/dir/a")].dvn_outpool = RT_NO_POOL;
	s.sc_names[name_at(&s, "/dir/b")].dvn_outpool = RT_NO_POOL;
	s.sc_names[name_at(&s, "/dir/a")].dvn_survives = 0;
	s.sc_pools[2].dvp_content = "nonsense";
	s.sc_pools[2].dvp_materialized = 1;
	expect(&spec, &s, "a quarantined region is not judged", 0, NULL);

	/*
	 * ... but it is still asserted to have been held back, and the
	 * fixture that says so is checked.
	 */
	{
		rt_spec_t q;
		rt_check_result_t res;

		(void) rt_spec_parse_text(
		    "tree base\n  1-1: /\n"
		    "tree onto\n  1-1: /\n"
		    "tree offof\n  1-1: /\n"
		    "expect quarantined /dir/a\n", "checkcheck-q", &q);

		scene_init(&s);
		(void) memset(&res, 0, sizeof (res));
		rt_tree_check_view(&q, &s.sc_dv, &res);
		cc_cases++;
		if (res.rcr_failures == 1 &&
		    strstr(res.rcr_first, "it was not") != NULL) {
			(void) printf("PASS  an unmet quarantine claim is "
			    "caught (%s)\n", res.rcr_first);
		} else {
			(void) printf("FAIL  an unmet quarantine claim: %d "
			    "complaint(s), first: %s\n", res.rcr_failures,
			    res.rcr_first);
			cc_failures++;
		}
		rt_spec_free(&q);
	}

	rt_spec_free(&spec);

	(void) printf("checkcheck: %d/%d cases behaved, %s\n",
	    cc_cases - cc_failures, cc_cases,
	    cc_failures == 0 ? "ALL CLEAN" : "FAILURES ABOVE");
	return (cc_failures == 0 ? 0 : 1);
}
