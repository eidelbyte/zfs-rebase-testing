/*
 * m1_smoke.c -- sprint-3 smoke: milestones M1 and M2.
 *
 * Exercises the v3 dsl_rebase() substrate and model end to end on
 * a real pool: ancestor discovery, preconditions, long holds, run
 * setup, the three-tree walk, the name table, the decide passes,
 * and the teardown ladder.  At this stage ENOSYS is the SUCCESS
 * signal: it means every stage ran and the engine stopped exactly
 * where emission will begin.  Covers milestones M1 (scaffolding)
 * and M2 (model census).
 *
 * The engine takes TWO SNAPSHOTS and creates nothing, so every
 * check snapshots its sides first (m1_rebase, or rt_rebase_snaps
 * when a pair is reused).  That also makes a stale kernel
 * unmissable: the arity did not change in the read-only rewrite,
 * so a pre-rewrite build links happily and then fails the very
 * first check with EINVAL instead of ENOSYS, because heads and
 * snapshots have swapped roles.
 *
 * Deliberately separate from the 265-test battery, which asserts
 * revision-2 engine behavior and does not link against a v3
 * libzpool.  This binary links only rt_harness.c, rt_scaffold.c,
 * and rt_zpl.c; the two revision-2 injection tunables the harness
 * declares are defined here because the v3 engine does not carry
 * them (revision 2 applied the merge in the kernel; revision 3
 * leaves that to userspace, so there is nothing to inject into).
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

static int m1_checks;
static int m1_failures;

/*
 * Decide across two datasets.  The engine reads snapshots and
 * creates nothing, so the harness snapshots both sides first; each
 * call gets a fresh generation, so a fixture can be mutated and
 * decided again with no cleanup in between.  A name that already
 * carries '@' is used as given.
 */
static int
m1_rebase(const char *offof_ds, const char *onto_ds)
{
	char offof[ZFS_MAX_DATASET_NAME_LEN];
	char onto[ZFS_MAX_DATASET_NAME_LEN];
	int err;

	err = rt_rebase_snaps(offof_ds, onto_ds, offof, onto,
	    sizeof (offof));
	if (err != 0)
		return (err);

	return (dsl_rebase(offof, onto, NULL));
}

/*
 * Assert that a stable debug line exists and carries the expected
 * census.  Substring rather than exact match, so the line's prefix
 * can gain context without breaking every caller.
 */
static void
check_line(const char *what, const char *needle, const char *expect)
{
	char line[512];

	m1_checks++;
	if (rt_dbgmsg_last(needle, line, sizeof (line)) != 0) {
		(void) printf("FAIL  %s: no debug line matching '%s'\n",
		    what, needle);
		m1_failures++;
		return;
	}
	if (strstr(line, expect) == NULL) {
		(void) printf("FAIL  %s:\n      expected: %s\n"
		    "      in line:  %s\n", what, expect, line);
		m1_failures++;
		return;
	}
	(void) printf("PASS  %s\n", what);
}

static void
check(const char *what, int got, int want)
{
	m1_checks++;
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
	rt_ds_t d;
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
	 * up to the emission boundary.  ENOSYS means every stage
	 * (discovery, preconditions, long holds, run setup, walk,
	 * and the decide passes) succeeded and the run tore down
	 * cleanly.
	 */
	check("substrate reaches the emit boundary (ENOSYS)",
	    m1_rebase(RT_DS_OFFOF, RT_DS_ONTO), ENOSYS);

	/*
	 * M2: the model built from that run.  rt_scaffold_basic()
	 * makes exactly four objects per tree -- the root, hello,
	 * subdir, and subdir/inner -- and the two sides are clones
	 * of one snapshot, so all three trees hold the same four
	 * names and the shared universe is four names wide.
	 */
	check_line("walk census: four pools in each tree",
	    "rebase: walk pools",
	    "base 4 onto 4 off-of 4, distinct names 4");
	check_line("name table census: four names, held by all three",
	    "rebase: name table",
	    "4 names, held base 4 onto 4 off-of 4");

	/*
	 * 2. The run is a pure function of its inputs.  Nothing is
	 * created, so nothing can strand and there is no state for a
	 * second run to trip over; deciding the SAME two snapshots
	 * again must give the same answer and the same census.  That
	 * is the read-only claim stated as a test: if any part of
	 * the engine wrote, the repeat would diverge.
	 */
	{
		char offof[ZFS_MAX_DATASET_NAME_LEN];
		char onto[ZFS_MAX_DATASET_NAME_LEN];

		err = rt_rebase_snaps(RT_DS_OFFOF, RT_DS_ONTO, offof,
		    onto, sizeof (offof));
		if (err != 0) {
			(void) printf("FAIL  snapshot the pair: %d\n", err);
			m1_checks++;
			m1_failures++;
		} else {
			check("re-deciding one pair repeats (run 1)",
			    dsl_rebase(offof, onto, NULL), ENOSYS);
			check("re-deciding one pair repeats (run 2)",
			    dsl_rebase(offof, onto, NULL), ENOSYS);
			check_line("and repeats the same census",
			    "rebase: walk pools",
			    "base 4 onto 4 off-of 4, distinct names 4");
		}
	}

	/*
	 * 3. Linear history: the onto snapshot already sits in the
	 * off-of clone's chain, so there is nothing to rebase.
	 */
	check("onto already in off-of's chain (EINVAL)",
	    m1_rebase(RT_DS_OFFOF, RT_DS_SRC "@base"), EINVAL);

	/*
	 * 3b. A live head is not an input.  The engine reads
	 * snapshots only, so it can promise the walk sees committed
	 * state that cannot move underneath it; a head fails up
	 * front rather than being fenced into one.  Each check
	 * swaps ONE side of a pair that decides cleanly, so an
	 * engine that quietly accepted heads would return ENOSYS
	 * here rather than EINVAL.
	 */
	{
		char offof[ZFS_MAX_DATASET_NAME_LEN];
		char onto[ZFS_MAX_DATASET_NAME_LEN];

		err = rt_rebase_snaps(RT_DS_OFFOF, RT_DS_ONTO, offof,
		    onto, sizeof (offof));
		if (err != 0) {
			(void) printf("FAIL  snapshot the pair: %d\n", err);
			m1_checks++;
			m1_failures++;
		} else {
			check("a live head is refused as off-of (EINVAL)",
			    dsl_rebase(RT_DS_OFFOF, onto, NULL), EINVAL);
			check("a live head is refused as onto (EINVAL)",
			    dsl_rebase(offof, RT_DS_ONTO, NULL), EINVAL);
		}
	}

	/*
	 * 4. The cell that actually exercises the pool model, since
	 * the base fixture is three identical clones with no
	 * hardlinks.  Off-of gains a hardlink -- a NAME with no new
	 * dnode -- while onto gains an ordinary file, a name AND a
	 * dnode.  The census then discriminates: off-of must report
	 * five held names but only four pools.  A model that counted
	 * a pool per name, or that missed the second link, cannot
	 * produce these numbers.
	 */
	err = rt_open(RT_DS_OFFOF, &d);
	if (err == 0) {
		uint64_t hello = 0, subdir = 0, inner = 0;

		err = rt_dir_lookup(d.rtd_os, d.rtd_root, "hello",
		    &hello);
		if (err == 0) {
			err = rt_add_hardlink(d.rtd_os, d.rtd_root,
			    "hello2", hello);
		}
		/*
		 * And one genuine edit, so the content census has
		 * something to find: without it every pool compares
		 * equal and the tier stack could be broken without
		 * the numbers moving.
		 */
		if (err == 0) {
			err = rt_dir_lookup(d.rtd_os, d.rtd_root,
			    "subdir", &subdir);
		}
		if (err == 0) {
			err = rt_dir_lookup(d.rtd_os, subdir, "inner",
			    &inner);
		}
		if (err == 0) {
			err = rt_edit_file(d.rtd_os, inner,
			    "edited\n", 7);
		}
		rt_close(&d);
	}
	if (err == 0) {
		err = rt_open(RT_DS_ONTO, &d);
		if (err == 0) {
			err = rt_create_file(d.rtd_os, d.rtd_root,
			    "extra", "x\n", 2, NULL);
			rt_close(&d);
		}
	}
	if (err != 0) {
		(void) printf("FAIL  build asymmetric fixture: %d\n", err);
		m1_checks++;
		m1_failures++;
	} else {
		rt_sync_pool();
		check("asymmetric trees reach the boundary (ENOSYS)",
		    m1_rebase(RT_DS_OFFOF, RT_DS_ONTO), ENOSYS);
		check_line("a hardlink adds a name, not a pool",
		    "rebase: walk pools",
		    "base 4 onto 5 off-of 4, distinct names 6");
		check_line("holders differ per tree",
		    "rebase: name table",
		    "6 names, held base 4 onto 5 off-of 5");
		/*
		 * Content: onto changed nothing that base holds (its
		 * new file has no base counterpart), so all four are
		 * unchanged there.  Off-of edited one file, so three.
		 * The two that must NOT move are the hardlinked file
		 * -- ZPL_LINKS is bookkeeping, not content -- and the
		 * root directory, whose entry count changed but whose
		 * attributes did not, directories being compared on
		 * attributes alone.
		 */
		check_line("a hardlink is not a content change",
		    "rebase: content",
		    "base pools 4, unchanged in onto 4 off-of 3");
		/*
		 * Faces.  Each tree pair shares the same four names,
		 * so four green edges apiece.  Fullness is where the
		 * hardlink shows: off-of's file holds two names while
		 * the edge carries one, so that edge is not full at
		 * the off-of end -- on the base-offof face and on the
		 * cut alike.  Red matches all four base objects on
		 * both faces (a clone keeps its origin's object
		 * numbers and generations), and composing those two
		 * matchings through base derives four across the cut
		 * without ever comparing the two sides directly.
		 */
		check_line("green edges, and fullness sees the hardlink",
		    "rebase: faces",
		    "green 4 4 4, full 4 3 3, red 4 4, derived 4");
		/*
		 * Pairing.  Every base pool has both nominations
		 * agreeing on the same partner, so all four pair on
		 * both base faces, and the derived red plus the sole
		 * green edge pair the same four across the cut.  The
		 * file onto added has neither nomination -- no base
		 * counterpart, no shared name -- so it pairs with
		 * nothing and must not inflate these counts.
		 */
		check_line("pools pair where both nominations agree",
		    "rebase: pairing",
		    "paired 4 4 4");
		/*
		 * Components.  Thirteen pools across the three trees
		 * (4 + 5 + 4) fall into five groups: each of base's
		 * four objects gathers its counterparts from both
		 * sides, and the file onto added stands alone because
		 * it shares no name and has no base identity.  A run
		 * that joined nothing would report thirteen.
		 */
		check_line("components gather each object's three copies",
		    "rebase: components",
		    "components 5 over 13 pools");
		/*
		 * Succession.  Off-of's pool gained /hello2 and lost
		 * nothing, so there is no rename to witness and the
		 * new name is an ATTACHMENT -- material joined to the
		 * pool, standing in for no base name.  Nothing was
		 * renamed anywhere, so no antecedents, nothing moved
		 * on both faces, and no conflict.  A rule that read
		 * any gain as a rename would report an antecedent
		 * here.
		 */
		/*
		 * The engine stamps its revision at the start of
		 * every run.  Assert it first: a libzpool built
		 * before the code under test otherwise shows up as a
		 * missing census line, which reads like a pass that
		 * ran and found nothing rather than a stale build.
		 */
		check_line("the engine under test is the one built",
		    "rebase: engine rev", "engine rev 7");
		check_line("a new link attaches, it does not succeed",
		    "rebase: succession",
		    "antecedents 0 0, attachments 0 1, moved-both 0, "
		    "conflicts 0");
		/*
		 * Lineage fates, counted unchanged / changed / dead /
		 * contested / disturbed.  Onto touched none of base's
		 * four pools, so all four continue unchanged there.
		 * Off-of changed two of them, and for different
		 * reasons that both have to be seen: the hardlinked
		 * file's NAME SET grew, and the edited file's CONTENT
		 * differs.  A pass that compared only names, or only
		 * content, would report one rather than two.  No pool
		 * is dead or contested, and every pool is unchanged on
		 * at least one side, so Rule 7.2 conflicts nothing.
		 */
		check_line("fates see both a name change and a byte change",
		    "rebase: lineage",
		    "onto 4/0/0/0/0, offof 2/2/0/0/0, conflicts 0");
		/*
		 * Pass 1, counted by row.  Four names sit where both
		 * sides left them (row 1).  /hello2 is home in onto
		 * only -- onto never had it -- so its cell ADOPTS
		 * off-of's arrangement (row 2).  /extra is the mirror:
		 * home in off-of only, so the cell KEEPS onto's (row
		 * 3).  Nothing needs the cut, and nothing conflicts.
		 * All six names survive: the fixture only ever added.
		 */
		check_line("cells adopt one side's addition and keep "
		    "the other's", "rebase: pass1",
		    "rows 4/1/1/0/0, survivors 6, deaths 0, conflicts 0");
		/*
		 * Pooling.  Six survivors become five pools, because
		 * /hello and /hello2 stay together -- and the clause
		 * that keeps them is L3, not L1 or L2.  L1 cannot
		 * fire: /hello2 has no base counterpart, so the pair
		 * was never co-pooled in base.  L2 cannot either:
		 * both names' material traces to the same base pool.
		 * It is the ATTACHMENT rule that holds the hardlink
		 * together, and if L3 were missing the count would be
		 * six pools, one per name.
		 */
		check_line("an attachment keeps the hardlink in one pool",
		    "rebase: pass2",
		    "pools 5, links 1 (L1 0, L2 0, L3 1), exclusions 0, "
		    "conflicts 0");
		/*
		 * Content.  Three pools short-circuit on agreement:
		 * the two sides hold identical bytes for the root,
		 * the hardlinked file and subdir.  Two need the
		 * three-way -- onto's new file, which only one side
		 * has, and the file off-of edited, where base still
		 * matches onto so off-of's edit is the change and
		 * wins.  Nothing conflicts, and every pool lands on an
		 * onto dnode, so nothing has to be materialized.
		 */
		check_line("agreement short-circuits, the edit wins its "
		    "three-way", "rebase: pass3",
		    "rules 3/0/2, onto-dnode 5, materialized 0, "
		    "conflicts 0");
		/*
		 * Ancestry and quarantine.  Every survivor's parent
		 * survived and decided to be a directory, and no
		 * directory pool ended up with two names, so nothing
		 * structural fires.  With no conflict anywhere there
		 * is nothing to seed the quarantine, so all five
		 * components emit -- which is the whole point of a
		 * clean merge.
		 */
		check_line("nothing structural, so nothing is held back",
		    "rebase: pass4", "structural conflicts 0");
		check_line("a clean decision quarantines nothing",
		    "rebase: quarantine",
		    "quarantine 0 of 5 components, conflicts 0");
	}

	/*
	 * 5. The first fixture that DISAGREES.  Onto deletes
	 * /subdir/inner while off-of edits it: the modify/delete
	 * that every merge tool has to answer for.
	 *
	 * What makes it worth asserting is where it is caught.  The
	 * name cell for /subdir/inner does not conflict at all -- it
	 * is home in off-of and absent from onto, so row 3 keeps
	 * onto's arrangement, which is that the name is gone, and the
	 * cell quietly records a death.  Pass 1 is structurally blind
	 * here, because it asks where a name lives and this is an
	 * argument about whether anything lives there.  Pass 0 is what
	 * sees it: base's pool is DEAD on onto and continued-changed
	 * on off-of, and Rule 7.2 conflicts that crossing.  The
	 * quarantine then holds exactly that component back, so
	 * off-of's edit is not silently discarded.
	 */
	err = rt_open(RT_DS_ONTO, &d);
	if (err == 0) {
		uint64_t subdir = 0;

		err = rt_dir_lookup(d.rtd_os, d.rtd_root, "subdir",
		    &subdir);
		if (err == 0)
			err = rt_remove_entry(d.rtd_os, subdir, "inner");
		rt_close(&d);
	}
	if (err != 0) {
		(void) printf("FAIL  build conflict fixture: %d\n", err);
		m1_checks++;
		m1_failures++;
	} else {
		rt_sync_pool();
		check("a conflicting merge still decides (ENOSYS)",
		    m1_rebase(RT_DS_OFFOF, RT_DS_ONTO), ENOSYS);
		check_line("pass 0 sees the modify/delete pass 1 cannot",
		    "rebase: lineage",
		    "onto 3/0/1/0/0, offof 2/2/0/0/0, conflicts 1");
		check_line("the deleted name dies without its cell "
		    "conflicting", "rebase: pass1",
		    "rows 3/1/2/0/0, survivors 5, deaths 1, conflicts 0");
		check_line("the conflict holds back exactly its component",
		    "rebase: quarantine",
		    "quarantine 1 of 5 components, conflicts 1");
	}

	/*
	 * 6. Unrelated datasets share no ancestor: the $ORIGIN
	 * guard stops both chain walks, so discovery reports ENOENT
	 * instead of finding the pool-global false ancestor.
	 */
	err = rt_create_zpl_dataset(POOL_NAME "/other");
	if (err != 0) {
		(void) printf("FAIL  create other: %d\n", err);
		m1_checks++;
		m1_failures++;
	} else {
		rt_sync_pool();
		check("unrelated datasets find no ancestor (ENOENT)",
		    m1_rebase(RT_DS_OFFOF, POOL_NAME "/other"), ENOENT);
	}

	rt_scaffold_teardown();
	kernel_fini();

	(void) printf("smoke (M1 scaffolding + M2 model): %d/%d passed, "
	    "%s\n", m1_checks - m1_failures, m1_checks,
	    m1_failures == 0 ? "ALL CLEAN" : "FAILURES ABOVE");
	return (m1_failures == 0 ? 0 : 1);
}
