#!/bin/sh
# Cross-parser gate for the .tree fixture corpus.
#
# The corpus is read by two independent parsers: the reference one in
# the demo's engine.js, which the page and the gold generator use, and
# the C one in rt_tree_parse.c, which the kernel harness uses. A
# fixture that means two different things to two readers is worse than
# no fixture, so this builds the C parser, dumps every fixture through
# both, and diffs.
#
#   devcheck/treecheck.sh
#
# The JavaScript half needs jsc and the decide-demo directory beside
# this repo. On a box that has neither -- which is every box that can
# actually run the suite -- that half is SKIPPED and said so; the C
# half still builds and parses, which catches the errors that matter
# there (a corrupt corpus copy, a fixture that no longer parses).
#
#   DEMO=/path/to/decide-demo devcheck/treecheck.sh
#   JSC=/path/to/jsc devcheck/treecheck.sh
#
# The corpus under devcheck/treecases/ is deliberately malformed: it
# exists so the ERROR paths are compared too, not just the happy ones.
# Every fixture is diffed; there is no exempt set. There used to be
# one, for the left/right tree names the two parsers refused in
# different words, and it is worth noting that it went away by the
# two sides AGREEING rather than by the check being relaxed.

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)

DEMO=${DEMO:-$ROOT/../zfs-rebase-theory/decide-demo}
JSC=${JSC:-/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0

# --- the corpus has not drifted from the demo's copy
echo "--- corpus"
devcheck/sync-trees.sh --check || fail=1

# --- the corpus, in a stable order
CORPUS=$(ls "$ROOT"/trees/*.tree "$ROOT"/trees/open-problems/*.tree \
    "$ROOT"/devcheck/treecases/*.tree 2>/dev/null)
if [ -z "$CORPUS" ]; then
	echo "no fixtures found under trees/ or devcheck/treecases/"
	exit 1
fi
NFIX=$(echo "$CORPUS" | wc -l | tr -d ' ')

# --- build the C parser and its dumper
echo "--- building treedump"
cc -std=c99 -Wall -Wextra -Wcast-qual -Werror -O1 \
    -o "$TMP/treedump" devcheck/treedump.c rt_tree_parse.c -I. || {
	echo "treedump FAILED to build"
	exit 1
}
echo "treedump OK"

echo "--- parsing $NFIX fixtures with the C parser"
# shellcheck disable=SC2086
"$TMP/treedump" $CORPUS > "$TMP/c.txt" || {
	echo "treedump FAILED to run"
	exit 1
}
echo "parsed OK ($(wc -l < "$TMP/c.txt" | tr -d ' ') lines of canonical dump)"

# --- the reference parser, when this machine has one
echo "--- cross-checking against the reference parser"
if [ ! -x "$JSC" ]; then
	echo "SKIP: no jsc at $JSC"
elif [ ! -f "$DEMO/engine.js" ]; then
	echo "SKIP: no engine.js at $DEMO"
else
	# shellcheck disable=SC2086
	( cd "$DEMO" && "$JSC" jsc-shim.js \
	    "$ROOT/devcheck/tree-canon.js" -- $CORPUS ) > "$TMP/js.txt" || {
		echo "reference parser FAILED to run"
		exit 1
	}
	if diff -u "$TMP/c.txt" "$TMP/js.txt" > "$TMP/diff.txt"; then
		echo "AGREE: both parsers produce the same canonical dump"
	else
		echo "DISAGREE: the two parsers read the corpus differently"
		echo "  (- is the C parser, + is the reference parser)"
		head -60 "$TMP/diff.txt"
		fail=1
	fi
fi

# --- the gold checker complains when it should
echo "--- checker self-test"
cc -std=c99 -Wall -Wextra -Wcast-qual -Werror -O1 \
    -o "$TMP/checkcheck" devcheck/checkcheck.c rt_tree_check.c \
    rt_tree_parse.c -I. || {
	echo "checkcheck FAILED to build"
	exit 1
}
"$TMP/checkcheck" || fail=1

# --- fixtures held out of the diff, and why
#
# A case the two parsers genuinely disagree about does not belong in
# the diff yet, and does not belong deleted either. It waits here and
# is announced on every run, so "we know about that one" cannot decay
# into nobody remembering it. This is not an exempt set: nothing here
# is checked, and the list is meant to reach zero.
if [ -d "$ROOT/devcheck/pending" ]; then
	for f in "$ROOT"/devcheck/pending/*.tree; do
		[ -e "$f" ] || continue
		echo "--- pending"
		echo "PENDING: $(basename "$f") is held out of the diff"
		sed -n '3,6p' "$f" | sed 's/^# \{0,1\}/         /'
	done
fi

# --- ASCII, everywhere
echo "--- ASCII"
for f in $CORPUS "$ROOT"/rt_tree.h "$ROOT"/rt_tree_parse.c \
    "$ROOT"/devcheck/treedump.c "$ROOT"/devcheck/tree-canon.js; do
	if LC_ALL=C grep -qn '[^ -~	]' "$f"; then
		echo "NON-ASCII in $f:"
		LC_ALL=C grep -n '[^ -~	]' "$f" | head -3
		fail=1
	fi
done
[ "$fail" -eq 0 ] && echo "ASCII OK"

[ "$fail" -eq 0 ] && echo "ALL CLEAN" || echo "FAILURES ABOVE"
exit "$fail"
