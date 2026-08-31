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

	if (argc < 2) {
		(void) fprintf(stderr, "usage: treedump <file.tree>...\n");
		return (2);
	}

	for (i = 1; i < argc; i++) {
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

		rt_spec_free(&spec);
	}

	return (failures > 0 ? 1 : 0);
}
