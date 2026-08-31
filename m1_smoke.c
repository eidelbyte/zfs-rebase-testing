/*
 * m1_smoke.c -- sprint-3 milestone M1: the scaffolding smoke.
 *
 * Exercises the v3 dsl_rebase() substrate end to end on a real
 * pool: ancestor discovery, preconditions, fence-post snapshots,
 * long holds, run setup, and the teardown ladder.  At this stage
 * ENOSYS is the SUCCESS signal: it means every scaffolding stage
 * ran and the engine stopped exactly where the walk will begin.
 *
 * Deliberately separate from the 265-test battery, which asserts
 * revision-2 engine behavior and does not link against a v3
 * libzpool.  This binary links only rt_harness.c, rt_scaffold.c,
 * and rt_zpl.c; the two revision-2 injection tunables the harness
 * declares are defined here because the v3 engine does not carry
 * them yet (they return with the apply driver).
 *
 * Build and run (FreeBSD, as root):
 *     make m1_smoke && sudo ./m1_smoke
 */

#include "rebase_test.h"

extern void kernel_init(int);
extern void kernel_fini(void);

/* v3 carries no apply injectors yet; satisfy the harness externs. */
uint64_t rebase_apply_inject_stop_after = 0;
int rebase_apply_inject_skip_rollback = 0;

static int m1_failures;

static void
check(const char *what, int got, int want)
{
	if (got == want) {
		(void) printf("PASS  %s (err %d)\n", what, got);
	} else {
		(void) printf("FAIL  %s: expected %d, got %d\n",
		    what, want, got);
		m1_failures++;
	}
}

int
main(void)
{
	int err;

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	err = rt_scaffold_basic();
	if (err != 0) {
		(void) printf("FAIL  scaffold: %d\n", err);
		kernel_fini();
		return (1);
	}

	/*
	 * 1. The substrate smoke: two clones of one snapshot rebase
	 * up to the walk boundary.  ENOSYS means every scaffolding
	 * stage (discovery, preconditions, both fences, long holds,
	 * run setup) succeeded and the run tore down cleanly.
	 */
	check("substrate reaches the walk boundary (ENOSYS)",
	    dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL), ENOSYS);

	/*
	 * 2. Fences must not strand.  The pre-apply lifecycle
	 * destroys both fences on every exit, so the fence is gone
	 * afterward and an identical second run repeats the result
	 * instead of failing EEXIST.
	 */
	check("left fence destroyed on exit",
	    rt_fence_exists() ? 1 : 0, 0);
	check("second run repeats (no EEXIST strand)",
	    dsl_rebase(RT_DS_LEFT, RT_DS_RIGHT, NULL), ENOSYS);

	/*
	 * 3. Linear history: the right snapshot already sits in the
	 * left clone's chain, so there is nothing to rebase.
	 */
	check("right already in left's chain (EINVAL)",
	    dsl_rebase(RT_DS_LEFT, RT_DS_SRC "@base", NULL), EINVAL);

	/*
	 * 4. Unrelated datasets share no ancestor: the $ORIGIN
	 * guard stops both chain walks, so discovery reports ENOENT
	 * instead of finding the pool-global false ancestor.
	 */
	err = rt_create_zpl_dataset(POOL_NAME "/other");
	if (err != 0) {
		(void) printf("FAIL  create other: %d\n", err);
		m1_failures++;
	} else {
		rt_sync_pool();
		check("unrelated datasets find no ancestor (ENOENT)",
		    dsl_rebase(RT_DS_LEFT, POOL_NAME "/other", NULL),
		    ENOENT);
	}

	rt_scaffold_teardown();
	kernel_fini();

	(void) printf("M1 smoke: %s\n",
	    m1_failures == 0 ? "ALL CLEAN" : "FAILURES ABOVE");
	return (m1_failures == 0 ? 0 : 1);
}
