// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * tree_suite -- run a corpus of .tree fixtures against the engine and
 * check what came out against the gold each fixture carries.
 *
 * One fixture is one whole scenario: three input trees, the output
 * they should decide to, and the handful of things an output tree
 * cannot say for itself.  The suite builds the trees on a real pool,
 * runs the engine, and compares.
 *
 * Build and run (FreeBSD, as root):
 *     make tree_suite && sudo ./tree_suite
 *     sudo ./tree_suite trees/open-problems/5-rename-rename.tree
 *
 * With no arguments it runs everything under trees/.
 *
 * WHAT IT CAN AND CANNOT PROVE TODAY.  The comparison that matters
 * -- name by name: which survived, which share an output pool, what
 * bytes it holds, whether the dnode was reused -- is written and
 * tested, in rt_tree_check.c.  What is missing is anything to feed
 * it: a decision has no external view until the manifest carries
 * one.  See the contract in rt_tree_suite.h.
 *
 * So this runs the CENSUS tier, off the engine's dbgmsg tallies,
 * which are counts.  A count can say five pools came out; it cannot
 * say WHICH names are in them.  It catches a fixture that produces
 * the wrong NUMBER of anything and is blind to one that produces the
 * right number of the wrong things.  Every line says CENSUS so
 * nobody mistakes it for the real thing.
 *
 * That is a real gate, not a placeholder, and it is what the first
 * box run should use: a failure in it is a statement about the
 * materializer or the fixture, which is exactly what needs proving
 * before anything is asserted about the engine.
 *
 * Deliberately separate from m1_smoke, which is the engine's own
 * milestone smoke and moves with the passes.  This binary moves with
 * the corpus.
 */

#include "rt_tree_suite.h"

#include <dirent.h>

extern void kernel_init(int);
extern void kernel_fini(void);

/* v3 carries no apply injectors yet; satisfy the harness externs. */
uint64_t rebase_apply_inject_stop_after = 0;
int rebase_apply_inject_skip_rollback = 0;

static int ts_fixtures;
static int ts_failed;
static int ts_skipped;
static int ts_checks;
static int ts_check_failures;
/*
 * Fixtures the engine refused with EINVAL.  Counted separately
 * because there is one cause that makes EVERY fixture fail
 * identically -- a libzpool older than the read-only rewrite, which
 * links fine (the arity did not change) and then rejects the
 * snapshots it is handed.  Said out loud at the end, because it
 * otherwise reads as a corpus that is entirely wrong.
 */
static int ts_einval;

/*
 * ------------------------------------------------------------------
 * Reading the census lines
 * ------------------------------------------------------------------
 */

/*
 * Pull one labelled number out of a dbgmsg tally: from
 * "rebase: pass2 pools 5, links 1 (...), exclusions 0, conflicts 0",
 * field "conflicts" gives 0.  Returns ENOENT when the line is not in
 * the ring at all, which is nearly always a stale build rather than a
 * pass that ran and found nothing.
 */
static int
census_u64(const char *needle, const char *field, uint64_t *out)
{
	char line[512];
	const char *p;
	int err;

	err = rt_dbgmsg_last(needle, line, sizeof (line));
	if (err != 0)
		return (err);

	p = strstr(line, field);
	if (p == NULL)
		return (ENOENT);
	p += strlen(field);
	while (*p == ' ')
		p++;
	if (*p < '0' || *p > '9')
		return (EINVAL);
	*out = strtoull(p, NULL, 10);
	return (0);
}

/* Every pass that reports a conflict count, and the kind it owns. */
static const struct {
	const char	*cp_needle;
	const char	*cp_label;
	int		cp_kind;
} ts_passes[] = {
	{ "rebase: lineage",	"lineage",	RT_CK_LINEAGE },
	{ "rebase: succession",	"succession",	RT_CK_NAME },
	{ "rebase: pass1",	"pass 1",	RT_CK_NAME },
	{ "rebase: pass2",	"pass 2",	RT_CK_POOLING },
	{ "rebase: pass3",	"pass 3",	RT_CK_CONTENT },
	{ "rebase: pass4",	"pass 4",	RT_CK_STRUCTURAL }
};

#define	TS_NPASSES	(int)(sizeof (ts_passes) / sizeof (ts_passes[0]))

/* How many conflicts, across every pass that owns this kind. */
static int
census_kind_total(int kind, uint64_t *totalp, int *sawp)
{
	int i;

	*totalp = 0;
	*sawp = 0;
	for (i = 0; i < TS_NPASSES; i++) {
		uint64_t v;

		if (ts_passes[i].cp_kind != kind)
			continue;
		if (census_u64(ts_passes[i].cp_needle, "conflicts", &v) != 0)
			continue;
		*sawp = 1;
		*totalp += v;
	}
	return (*sawp ? 0 : ENOENT);
}

/* Distinct names and pools a gold tree asserts. */
static void
gold_shape(const rt_spec_t *sp, int *nnamesp, int *npoolsp, int *nfreshp)
{
	const rt_tree_t *exp = &sp->rts_trees[RT_TREE_EXPECTED];
	const rt_tree_t *onto = &sp->rts_trees[RT_TREE_ONTO];
	int i;

	*nnamesp = 0;
	*npoolsp = exp->rtt_npools;
	*nfreshp = 0;
	for (i = 0; i < exp->rtt_npools; i++) {
		*nnamesp += exp->rtt_pools[i].rtp_nnames;
		if (rt_tree_by_key(onto, exp->rtt_pools[i].rtp_key) == NULL)
			(*nfreshp)++;
	}
}

/*
 * ------------------------------------------------------------------
 * The input census: did the materializer build what the fixture says?
 * ------------------------------------------------------------------
 *
 * These numbers describe the three INPUT trees, not the decision, so
 * the fixture predicts them on its own and gold is not involved.
 * That matters for two reasons.
 *
 * They apply to every fixture, conflicted ones included, where the
 * gold-derived counts below only work on clean runs.  And they check
 * the MATERIALIZER, which is the part of this suite that has never
 * run: if the engine sees four base pools where the fixture describes
 * five, the fixture was built wrong and nothing downstream of that
 * means anything.  A first box run wants to answer that question
 * before any other.
 */

/* Assert one census field against a number the fixture knows. */
static void
census_is(rt_check_result_t *res, const char *needle, const char *field,
    int want, const char *what)
{
	uint64_t got;
	int err = census_u64(needle, field, &got);

	if (err != 0) {
		rt_check(res, 0, "no census line for %s (stale build?)", what);
		return;
	}
	rt_check(res, got == (uint64_t)want, "%s: fixture says %d, engine "
	    "saw %llu", what, want, (unsigned long long)got);
}

void
rt_tree_check_inputs(const rt_spec_t *sp, rt_check_result_t *res)
{
	const rt_tree_t *b = &sp->rts_trees[RT_TREE_BASE];
	const rt_tree_t *o = &sp->rts_trees[RT_TREE_ONTO];
	const rt_tree_t *f = &sp->rts_trees[RT_TREE_OFFOF];
	int names = rt_spec_union_names(sp);

	census_is(res, "rebase: walk pools", "pools base", b->rtt_npools,
	    "base pools");
	census_is(res, "rebase: walk pools", "onto", o->rtt_npools,
	    "onto pools");
	census_is(res, "rebase: walk pools", "off-of", f->rtt_npools,
	    "off-of pools");
	census_is(res, "rebase: walk pools", "distinct names", names,
	    "distinct names");

	/*
	 * The name table's count restates the walk's distinct names
	 * from a different structure, so asserting both catches an
	 * engine that disagrees with itself as well as a fixture that
	 * was built wrong.
	 */
	census_is(res, "rebase: name table", "name table", names,
	    "name table size");
	census_is(res, "rebase: name table", "held base",
	    rt_tree_nnames(b), "names held in base");
	census_is(res, "rebase: name table", "onto", rt_tree_nnames(o),
	    "names held in onto");
	census_is(res, "rebase: name table", "off-of", rt_tree_nnames(f),
	    "names held in off-of");

	census_is(res, "rebase: content", "base pools", b->rtt_npools,
	    "base pools reaching the content tier");
	census_is(res, "rebase: components", "over",
	    b->rtt_npools + o->rtt_npools + f->rtt_npools,
	    "pools entering components");
}

/*
 * The weak tier.  What it CAN establish: that the passes reported the
 * conflict kinds gold claims and no others when gold says clean, and
 * -- only for a clean fixture -- that the number of survivors, output
 * pools and materializations matches what gold describes.
 *
 * What it CANNOT establish: which names those are.  The counting
 * checks are also restricted to clean fixtures on purpose.  A
 * quarantined component is still DECIDED, so its pools are still in
 * pass 2's tally, while the expected tree shows onto's arrangement
 * for that region instead; comparing the two would be comparing
 * different things and would fail on correct engines.
 */
void
rt_tree_check_census(const rt_spec_t *sp, rt_check_result_t *res)
{
	const rt_tree_t *exp = &sp->rts_trees[RT_TREE_EXPECTED];
	int wants_clean = 0;
	int i;
	uint64_t v;

	for (i = 0; i < sp->rts_nexpects; i++) {
		if (sp->rts_expects[i].rte_kind == RT_EXP_CLEAN)
			wants_clean = 1;
	}

	for (i = 0; i < sp->rts_nexpects; i++) {
		const rt_expect_t *ex = &sp->rts_expects[i];
		uint64_t total;
		int saw;
		int j;

		switch (ex->rte_kind) {
		case RT_EXP_CLEAN:
			for (j = 0; j < TS_NPASSES; j++) {
				if (census_u64(ts_passes[j].cp_needle,
				    "conflicts", &v) != 0) {
					rt_check(res, 0, "no %s census line "
					    "in the ring (stale build?)",
					    ts_passes[j].cp_label);
					continue;
				}
				rt_check(res, v == 0, "expected a clean run, "
				    "%s reported %llu conflict(s)",
				    ts_passes[j].cp_label,
				    (unsigned long long)v);
			}
			if (census_u64("rebase: quarantine", "quarantine",
			    &v) == 0) {
				rt_check(res, v == 0, "expected a clean run, "
				    "%llu component(s) quarantined",
				    (unsigned long long)v);
			}
			break;

		case RT_EXP_CONFLICT:
			if (census_kind_total(rt_conflict_kind_bit(
			    ex->rte_arg), &total, &saw) != 0) {
				rt_check(res, 0, "no census line for %s "
				    "conflicts (stale build?)", ex->rte_arg);
				break;
			}
			rt_check(res, total > 0, "expected a %s conflict, the "
			    "census counted none", ex->rte_arg);
			break;

		case RT_EXP_QUARANTINED:
			/*
			 * A count cannot name a path.  All this
			 * establishes is that SOMETHING was held back;
			 * the gold tier is what will name it.
			 */
			if (census_u64("rebase: quarantine", "quarantine",
			    &v) != 0) {
				rt_check(res, 0, "no quarantine census line "
				    "(stale build?)");
				break;
			}
			rt_check(res, v > 0, "expected %s to be quarantined, "
			    "nothing was", ex->rte_arg);
			break;
		default:
			break;
		}
	}

	if (!wants_clean || exp->rtt_npools == 0)
		return;

	{
		int nnames, npools, nfresh;

		gold_shape(sp, &nnames, &npools, &nfresh);

		if (census_u64("rebase: pass1", "survivors", &v) == 0) {
			rt_check(res, v == (uint64_t)nnames, "gold lists %d "
			    "surviving name(s), pass 1 counted %llu", nnames,
			    (unsigned long long)v);
		}
		if (census_u64("rebase: pass2", "pools", &v) == 0) {
			rt_check(res, v == (uint64_t)npools, "gold lists %d "
			    "output pool(s), pass 2 counted %llu", npools,
			    (unsigned long long)v);
		}
		if (census_u64("rebase: pass3", "materialized", &v) == 0) {
			rt_check(res, v == (uint64_t)nfresh, "gold implies %d "
			    "materialization(s), pass 3 counted %llu", nfresh,
			    (unsigned long long)v);
		}
	}
}

/*
 * ------------------------------------------------------------------
 * One fixture
 * ------------------------------------------------------------------
 */

static void
run_fixture(const char *path)
{
	rt_spec_t spec;
	rt_check_result_t res;
	char err[512];
	const char *base;
	int rc;

	base = strrchr(path, '/');
	base = base != NULL ? base + 1 : path;

	(void) memset(&res, 0, sizeof (res));
	ts_fixtures++;

	rc = rt_spec_parse_file(path, &spec);
	if (rc != 0) {
		(void) printf("FAIL  %-36s does not parse: %s\n", base,
		    spec.rts_nerrors > 0 ? spec.rts_errors[0] :
		    strerror(rc));
		ts_failed++;
		rt_spec_free(&spec);
		return;
	}

	if (spec.rts_trees[RT_TREE_EXPECTED].rtt_npools == 0 &&
	    spec.rts_nexpects == 0) {
		(void) printf("SKIP  %-36s carries no gold to check\n", base);
		ts_skipped++;
		rt_spec_free(&spec);
		return;
	}

	rc = rt_tree_materialize(&spec, err, sizeof (err));
	if (rc == ENOTSUP) {
		(void) printf("SKIP  %-36s %s\n", base, err);
		ts_skipped++;
		goto out;
	}
	if (rc != 0) {
		(void) printf("FAIL  %-36s cannot be built: %s\n", base, err);
		ts_failed++;
		goto out;
	}

	/*
	 * ENOSYS is accepted because in the current era it means every
	 * pass ran and the engine stopped where reporting will begin.
	 * When the manifest carries a decision, this is where reading
	 * it goes -- and rt_tree_check_view() is already written and
	 * tested for that, waiting only on something to fill a view.
	 */
	rc = rt_engine_run(NULL);
	if (rc != ENOSYS && rc != 0) {
		(void) printf("FAIL  %-36s engine refused: %s (%d)\n",
		    base, strerror(rc), rc);
		if (rc == EINVAL)
			ts_einval++;
		ts_failed++;
		goto out;
	}
	/*
	 * Inputs first.  If the fixture was not built as described,
	 * every later complaint is downstream of that and reading them
	 * is a waste of a box run.
	 */
	rt_tree_check_inputs(&spec, &res);
	rt_tree_check_census(&spec, &res);

	ts_checks += res.rcr_checks;
	ts_check_failures += res.rcr_failures;

	if (res.rcr_failures > 0) {
		(void) printf("FAIL  %-36s %s\n", base, res.rcr_first);
		if (res.rcr_failures > 1) {
			(void) printf("      (and %d more)\n",
			    res.rcr_failures - 1);
		}
		ts_failed++;
	} else if (res.rcr_checks == 0) {
		(void) printf("SKIP  %-36s nothing checkable\n", base);
		ts_skipped++;
	} else {
		(void) printf("PASS  %-36s %d check(s) CENSUS\n", base,
		    res.rcr_checks);
	}

out:
	rt_scaffold_teardown();
	rt_spec_free(&spec);
}

/*
 * ------------------------------------------------------------------
 * Corpus discovery
 * ------------------------------------------------------------------
 */

static int
str_cmp(const void *a, const void *b)
{
	return (strcmp(*(char * const *)a, *(char * const *)b));
}

/* One level of nesting, which is all the corpus layout uses. */
static char **
collect(const char *dir, int *np)
{
	char **out = NULL;
	int n = 0;
	DIR *d;
	struct dirent *de;

	d = opendir(dir);
	if (d == NULL)
		return (NULL);

	while ((de = readdir(d)) != NULL) {
		char full[1024];
		size_t len = strlen(de->d_name);

		if (de->d_name[0] == '.')
			continue;
		(void) snprintf(full, sizeof (full), "%s/%s", dir,
		    de->d_name);

		if (len > 5 && strcmp(de->d_name + len - 5, ".tree") == 0) {
			out = rt_xrealloc(out,
			    (size_t)(n + 1) * sizeof (char *));
			out[n++] = rt_xstrdup(full);
			continue;
		}

		{
			int sn = 0;
			char **sub = collect(full, &sn);
			int i;

			for (i = 0; i < sn; i++) {
				out = rt_xrealloc(out,
				    (size_t)(n + 1) * sizeof (char *));
				out[n++] = sub[i];
			}
			free(sub);
		}
	}
	(void) closedir(d);

	qsort(out, (size_t)n, sizeof (char *), str_cmp);
	*np = n;
	return (out);
}

int
main(int argc, char **argv)
{
	char line[512];
	char **paths = NULL;
	int npaths = 0;
	int owned = 0;
	int i;

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	if (argc > 1) {
		paths = argv + 1;
		npaths = argc - 1;
	} else {
		paths = collect("trees", &npaths);
		owned = 1;
		if (paths == NULL || npaths == 0) {
			(void) printf("no fixtures under trees/ -- run from "
			    "the repo root, or name files on the command "
			    "line\n");
			kernel_fini();
			return (2);
		}
	}

	(void) printf("tree suite: %d fixture(s), CENSUS tier "
	    "(counts only)\n", npaths);
	(void) printf("      A count can say five pools came out; it "
	    "cannot say WHICH names\n      are in them. Waiting on the "
	    "manifest -- see rt_tree_suite.h.\n");

	for (i = 0; i < npaths; i++)
		run_fixture(paths[i]);

	/*
	 * Report the engine revision last, when the ring is certain to
	 * hold one: a mismatch between the binary under test and the
	 * code being read is the failure that wastes the most time.
	 */
	if (rt_dbgmsg_last("rebase: engine rev", line, sizeof (line)) == 0)
		(void) printf("\n%s", line);

	/*
	 * Every fixture refused the same way is a statement about the
	 * BUILD, not about the corpus.  The likeliest cause by far is
	 * a libzpool from before the engine went read-only: the arity
	 * of dsl_rebase() did not change in that rewrite, so a stale
	 * one links happily and then rejects the snapshots.
	 */
	if (ts_einval > 0 && ts_einval == ts_fixtures - ts_skipped) {
		(void) printf("\nEvery fixture was refused with EINVAL. "
		    "That is almost certainly a stale\nlibzpool rather than "
		    "a corpus problem -- the read-only rewrite kept\n"
		    "dsl_rebase()'s arity, so an old one links and then "
		    "rejects its inputs.\nRebuild and reinstall libzpool, "
		    "then rerun.\n");
	}

	(void) printf("tree suite: %d/%d fixtures passed (%d skipped), "
	    "%d/%d checks, %s\n", ts_fixtures - ts_failed - ts_skipped,
	    ts_fixtures - ts_skipped, ts_skipped,
	    ts_checks - ts_check_failures, ts_checks,
	    ts_failed == 0 ? "ALL CLEAN" : "FAILURES ABOVE");

	if (owned) {
		for (i = 0; i < npaths; i++)
			free(paths[i]);
		free(paths);
	}

	kernel_fini();
	return (ts_failed == 0 ? 0 : 1);
}
