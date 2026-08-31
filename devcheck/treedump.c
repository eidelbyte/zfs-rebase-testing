// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * treedump -- print the canonical parse of one or more .tree files.
 *
 * The .tree parser has no ZFS dependency, which is the whole reason
 * this program can exist: it builds and runs on a laptop with no
 * kernel, no pool and no box, so the parser half of the fixture
 * loader is testable where the code is being written rather than
 * only where it is being run.
 *
 * devcheck/treecheck.sh diffs this output against the same dump
 * taken from the reference parser in JavaScript.  Two independent
 * readers of one corpus are only worth having if they agree.
 *
 *   cc -o treedump devcheck/treedump.c rt_tree_parse.c -I.
 *   ./treedump trees/hardlink-farm.tree
 */

#include "../rt_tree.h"

#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	int i;
	int failures = 0;
	int census = 0;
	int first = 1;

	if (argc > 1 && strcmp(argv[1], "-c") == 0) {
		census = 1;
		first = 2;
	}
	if (argc <= first) {
		(void) fprintf(stderr,
		    "usage: treedump [-c] <file.tree>...\n");
		return (2);
	}

	for (i = first; i < argc; i++) {
		rt_spec_t spec;
		const char *base;
		int rc;

		rc = rt_spec_parse_file(argv[i], &spec);
		if (rc != 0 && spec.rts_path == NULL) {
			(void) fprintf(stderr, "treedump: %s: cannot read\n",
			    argv[i]);
			failures++;
			continue;
		}

		/*
		 * Name the fixture by basename so the dump does not
		 * change when the corpus moves between the theory
		 * directory and the harness copy.
		 */
		base = strrchr(argv[i], '/');
		base = base != NULL ? base + 1 : argv[i];
		(void) printf("spec %s\n", base);
		rt_spec_dump(&spec, stdout);

		/*
		 * The input census the suite predicts, printed so it can
		 * be checked here rather than only on a box.  Not part
		 * of the cross-parser diff -- the reference engine has
		 * no such notion -- so it goes behind a flag.
		 */
		if (census) {
			(void) printf("census walk pools base %d onto %d "
			    "off-of %d, distinct names %d\n",
			    spec.rts_trees[RT_TREE_BASE].rtt_npools,
			    spec.rts_trees[RT_TREE_ONTO].rtt_npools,
			    spec.rts_trees[RT_TREE_OFFOF].rtt_npools,
			    rt_spec_union_names(&spec));
			(void) printf("census name table %d names, held "
			    "base %d onto %d off-of %d\n",
			    rt_spec_union_names(&spec),
			    rt_tree_nnames(&spec.rts_trees[RT_TREE_BASE]),
			    rt_tree_nnames(&spec.rts_trees[RT_TREE_ONTO]),
			    rt_tree_nnames(&spec.rts_trees[RT_TREE_OFFOF]));
			(void) printf("census components over %d pools\n",
			    spec.rts_trees[RT_TREE_BASE].rtt_npools +
			    spec.rts_trees[RT_TREE_ONTO].rtt_npools +
			    spec.rts_trees[RT_TREE_OFFOF].rtt_npools);
		}

		rt_spec_free(&spec);
	}

	return (failures > 0 ? 1 : 0);
}
