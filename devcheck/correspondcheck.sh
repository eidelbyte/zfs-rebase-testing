#!/bin/sh
# Run the Section 16 correspondence phase over the op3 corpus and
# check its believed moves against the fixtures' hand-worked answers.
#
#   devcheck/correspondcheck.sh
#   DEMO=/path/to/decide-demo devcheck/correspondcheck.sh
#   JSC=/path/to/jsc devcheck/correspondcheck.sh
#
# Needs jsc and the demo directory, because the fixtures are read with
# the reference parser -- deliberately, so that a disagreement here is
# about Section 16 and never about how the two parsers read a .tree
# file.  That question already has its own gate in treecheck.sh, and
# an implementation should not be judged against input it parsed
# itself.
#
# On a box with neither, this SKIPS and says so.  It is a desk check,
# not a box check: nothing here touches a pool.
#
# A DISAGREEMENT IS A FINDING.  The runner prints the hand-worked
# answer and the computed one side by side; do not edit either to
# match the other before deciding which reading of Section 16 is
# wrong.

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)

DEMO=${DEMO:-$ROOT/../zfs-rebase-theory/decide-demo}
JSC=${JSC:-/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc}

if [ ! -x "$JSC" ]; then
	echo "SKIP: no jsc at $JSC"
	exit 0
fi
if [ ! -f "$DEMO/engine.js" ]; then
	echo "SKIP: no engine.js at $DEMO"
	exit 0
fi

CORPUS=$(ls "$ROOT"/trees/open-problems/op3-*.tree 2>/dev/null || true)
if [ -z "$CORPUS" ]; then
	echo "no op3 fixtures under trees/open-problems/"
	exit 1
fi

# shellcheck disable=SC2086
( cd "$DEMO" && "$JSC" jsc-shim.js "$ROOT/devcheck/correspondcheck.js" \
    -- $CORPUS )
