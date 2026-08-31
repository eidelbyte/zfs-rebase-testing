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
# Fixtures under treecases/c-only/ are where the two parsers disagree
# on purpose, and are checked against an expected message instead.

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

# --- where the two parsers refuse the same thing in different words
#
# These used to be a real divergence: the reference accepted the
# left/right aliases and bound them backwards. It refuses them now
# too, so all that is left is wording -- the C parser says WHY, since
# the person who hits it is writing a fixture and needs to know the
# polarity, while the reference just calls the name unknown. Kept out
# of the byte-exact diff for that reason, and checked here instead,
# so "they only differ in wording" stays a verified claim.
echo "--- deliberate divergences (wording only)"
for f in "$ROOT"/devcheck/treecases/c-only/*.tree; do
	[ -e "$f" ] || continue
	case "$(basename "$f")" in
	err-alias.tree)
		cgot=$("$TMP/treedump" "$f" |
		    grep -c "ambiguous against harness polarity" || true)
		if [ "$cgot" -eq 2 ]; then
			echo "C parser refuses both aliases: OK"
		else
			echo "C parser refuses both aliases: FAIL" \
			    "(got $cgot, want 2)"
			fail=1
		fi

		if [ -x "$JSC" ] && [ -f "$DEMO/engine.js" ]; then
			jgot=$( ( cd "$DEMO" && "$JSC" jsc-shim.js \
			    "$ROOT/devcheck/tree-canon.js" -- "$f" ) |
			    grep -c "unknown tree name" || true)
			if [ "$jgot" -eq 2 ]; then
				echo "reference refuses both aliases: OK"
			else
				echo "reference refuses both aliases: FAIL" \
				    "(got $jgot, want 2) -- if it ACCEPTS" \
				    "them again, the aliases are back and" \
				    "bind backwards"
				fail=1
			fi
		fi
		;;
	*)
		echo "no expectation recorded for $(basename "$f")"
		fail=1
		;;
	esac
done

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
