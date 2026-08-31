#!/bin/sh
# Dev gate for the .tree suite's ZFS-dependent sources.
#
# The suite splits deliberately: the parser and the gold checker have
# no ZFS dependency and are BUILT AND RUN on this machine by
# devcheck/treecheck.sh and devcheck/checkcheck.sh.  What is left --
# the materializer, the decision adapter, the driver -- needs a pool
# and cannot be run here, so it gets a syntax and arity check instead,
# against the real revision-3 contract header.
#
#   devcheck/suitecheck.sh
#   ZFS_SRC=/path/to/openzfs devcheck/suitecheck.sh
#
# The point of using the REAL sys/dsl_rebase.h rather than a fake is
# that rebase_decision_t is exactly what the adapter reads: a fake
# would make the check agree with itself instead of with the engine.
# Only the harness rt_* API is stubbed, in devcheck/stub-suite/.
#
# This catches misspelled fields, wrong arity, dropped const, and
# unused variables. It cannot catch a wrong ANSWER; the FreeBSD build
# and run remain the authority.

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)

ZFS_SRC=${ZFS_SRC:-$ROOT/../zfs}
HDRSTUB=${HDRSTUB:-$ROOT/../scripts/header-stub}

SRCS="rt_tree_build.c rt_decision.c tree_suite.c"

if [ ! -f "$ZFS_SRC/include/sys/dsl_rebase.h" ]; then
	echo "SKIP: no sys/dsl_rebase.h under $ZFS_SRC/include"
	echo "      (set ZFS_SRC to the openzfs tree to enable this gate)"
	exit 0
fi
if [ ! -d "$HDRSTUB/sys" ]; then
	echo "SKIP: no header stub tree at $HDRSTUB"
	exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cp devcheck/stub-suite/rebase_test.h "$TMP/rebase_test.h"
cp rt_tree.h rt_tree_check.h rt_tree_suite.h $SRCS "$TMP/"

fail=0

# Compiled BOTH ways. Without the macro is what ships today; with it
# is the inspection-seam path, which is dead code until the seam
# lands and would therefore rot unwatched -- and it is precisely the
# code that has to be right on the day it switches on.
for mode in "" "-DRT_HAVE_DECIDE_ACCESSOR"; do
	if [ -z "$mode" ]; then
		label="census tier"
	else
		label="inspection seam"
	fi
	for f in $SRCS; do
		if cc -fsyntax-only -std=c99 -Wall -Wextra -Wcast-qual \
		    -Werror -Wno-unused-function $mode \
		    -I"$TMP" -I"$HDRSTUB" -I"$ZFS_SRC/include" \
		    "$TMP/$f" 2>&1; then
			echo "compile OK: $f ($label)"
		else
			echo "compile FAIL: $f ($label)"
			fail=1
		fi
	done
done

echo "--- ASCII"
for f in $SRCS rt_tree.h rt_tree_check.h rt_tree_suite.h rt_tree_check.c \
    devcheck/stub-suite/rebase_test.h devcheck/checkcheck.c; do
	if LC_ALL=C grep -qn '[^ -~	]' "$f"; then
		echo "NON-ASCII in $f:"
		LC_ALL=C grep -n '[^ -~	]' "$f" | head -3
		fail=1
	fi
done
[ "$fail" -eq 0 ] && echo "ASCII OK"

[ "$fail" -eq 0 ] && echo "ALL CLEAN" || echo "FAILURES ABOVE"
exit "$fail"
