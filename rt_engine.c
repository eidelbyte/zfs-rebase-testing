// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * The one place the suite calls the engine.
 *
 * dsl_rebase()'s signature has moved twice already -- it lost a
 * fence-post era, and gained and then lost an explicit base argument
 * -- so it is worth exactly one function of indirection.
 *
 * This file used to hold something larger: an adapter that read
 * rebase_decision_t through a proposed in-kernel inspection
 * callback.  That seam was cancelled in favour of the output
 * manifest, which carries the same facts in the form users actually
 * see.  The adapter is deleted rather than left behind a macro,
 * because machinery guarding a case that cannot occur is a
 * liability and nothing would have exercised it.
 *
 * What the suite needs from the manifest, field by field, is written
 * out in rt_tree_suite.h.  Nothing here is waiting on it: the
 * comparison itself lives in rt_tree_check.c and works on a flat
 * view, so whatever fills that view in is a separate, small piece of
 * code that will be written when there is something to read.
 */

#include "rt_tree_suite.h"

/*
 * Base is not passed: it is the common ancestor of the two snapshot
 * chains and the engine discovers it, so every fixture in the corpus
 * exercises that discovery as a side effect of doing anything at all.
 */
int
rt_engine_run(nvlist_t *outnvl)
{
	return (dsl_rebase(RT_SNAP_OFFOF, RT_SNAP_ONTO, outnvl));
}
