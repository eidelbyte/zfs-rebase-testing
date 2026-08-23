#!/bin/sh
# Harness dev battery for machines with no ZFS headers (the mac):
# syntax-check every harness source against the stub header, then
# run the cheap consistency checks. Run from the repo root or from
# devcheck/; checks the whole repo either way.
#
#   devcheck/syncheck.sh            # all .c files
#   devcheck/syncheck.sh test_hysteria.c rt_zpl.c
#
# The compile catches syntax, call arity against the stub
# prototypes, unused variables, and const-dropping casts
# (-Wcast-qual, which the FreeBSD kernel build -Werrors on -- the
# harness does not, but the habit is free here). The consistency
# pass catches drift the compiler cannot: a test defined but never
# called from its section runner, unbalanced braces in macro-heavy
# code, and stray non-ASCII. The FreeBSD build and run remain the
# authority; this is the pre-push gate.

set -e

cd "$(dirname "$0")/.."
FILES="$*"
[ -z "$FILES" ] && FILES=$(ls *.c)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp devcheck/stub/rebase_test.h "$TMP/rebase_test.h"
cp $FILES "$TMP/"

fail=0
for f in $FILES; do
	if cc -fsyntax-only -Wall -Wextra -Wcast-qual -Werror \
	    -Wno-unused-function -std=c99 "$TMP/$f" 2>&1; then
		echo "compile OK: $f"
	else
		echo "compile FAIL: $f"
		fail=1
	fi
done

echo "--- consistency"
for f in $FILES; do
	case "$f" in
	test_*.c)
		def=$(grep -c '^test_' "$f" || true)
		call=$(grep -c '(void) test_' "$f" || true)
		if [ "$def" -ne "$call" ]; then
			echo "MISMATCH: $f defines $def tests," \
			    "runner calls $call"
			fail=1
		else
			echo "tests OK: $f ($def defined == called)"
		fi
		;;
	esac
	bal=$(awk '{ for (i = 1; i <= length($0); i++) {
		c = substr($0, i, 1)
		if (c == "{") d++; if (c == "}") d-- } }
	    END { print d }' "$f")
	if [ "$bal" -ne 0 ]; then
		echo "BRACES: $f off by $bal"
		fail=1
	fi
	if LC_ALL=C grep -qn '[^ -~	]' "$f"; then
		echo "NON-ASCII in $f:"
		LC_ALL=C grep -n '[^ -~	]' "$f" | head -3
		fail=1
	fi
done

[ "$fail" -eq 0 ] && echo "ALL CLEAN" || echo "FAILURES ABOVE"
exit "$fail"
