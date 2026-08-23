// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, Eidel Solomon. All rights reserved.
 *
 * Entry point for the zfs rebase test suite. With no arguments,
 * runs every section; with arguments, runs only the named sections:
 *
 *   sudo ./rebase_test              # everything
 *   sudo ./rebase_test moves        # one section
 *   sudo ./rebase_test basic linkpool
 */

#include "rebase_test.h"

extern void kernel_init(int);
extern void kernel_fini(void);

static const struct {
	const char	*s_name;
	void		(*s_run)(void);
} sections[] = {
	{ "basic",	run_basic_tests },
	{ "setup",	run_setup_tests },
	{ "walk",	run_walk_tests },
	{ "hysteria",	run_hysteria_tests },
	{ "diff",	run_diff_tests },
	{ "moves",	run_moves_tests },
	{ "anchor",	run_anchor_tests },
	{ "linkpool",	run_linkpool_tests },
	{ "crossref",	run_crossref_tests },
};

#define	NSECTIONS	(sizeof (sections) / sizeof (sections[0]))

static int
run_section(const char *name)
{
	for (size_t i = 0; i < NSECTIONS; i++) {
		if (strcmp(sections[i].s_name, name) == 0) {
			sections[i].s_run();
			return (0);
		}
	}
	return (-1);
}

int
main(int argc, char **argv)
{
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	(void) printf("zfs rebase test suite\n");
	(void) printf("=====================\n");

	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			if (run_section(argv[i]) != 0) {
				(void) fprintf(stderr,
				    "unknown section '%s'; sections:",
				    argv[i]);
				for (size_t j = 0; j < NSECTIONS; j++)
					(void) fprintf(stderr, " %s",
					    sections[j].s_name);
				(void) fprintf(stderr, "\n");
				kernel_fini();
				return (2);
			}
		}
	} else {
		for (size_t i = 0; i < NSECTIONS; i++)
			sections[i].s_run();
	}

	(void) printf("\n=====================\n");
	(void) printf("Results: %d/%d passed",
	    rt_tests_passed, rt_tests_run);
	if (rt_tests_failed > 0)
		(void) printf(", %d FAILED", rt_tests_failed);
	(void) printf("\n");

	kernel_fini();

	return (rt_tests_failed > 0 ? 1 : 0);
}
