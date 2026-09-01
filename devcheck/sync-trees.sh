#!/bin/sh
# Keep the .tree corpus in step with the demo's copy.
#
# The fixtures are authored beside the reference engine, in the
# decide-demo directory, where the page renders them and the gold
# generator writes their expected blocks. The kernel harness needs its
# own copy, because the harness repo is what gets pushed to the box and
# the demo directory is not version controlled at all.
#
# Two copies of anything drift. So the copies are checksummed into
# trees/MANIFEST, and the check mode verifies them -- which is what
# devcheck/treecheck.sh runs. A fixture that changed on one side and
# not the other stops being a shared artifact and starts being two
# artifacts that happen to look alike, which is the failure this
# exists to make loud.
#
# Drift runs in two directions, and this script only used to watch
# one. It walked the demo's fixtures, so it saw a fixture that had
# changed and a fixture the harness was missing, and was blind to one
# that exists only here -- reporting "byte-identical" while this side
# held fourteen fixtures the demo had never heard of. The reverse
# scan below closes that.
#
# One stale premise, corrected: the note above used to say the demo
# directory is not version controlled at all. As of 2026-08-31 the
# parent repository whitelists zfs-rebase-theory/, so it is. That was
# the whole reason for keeping two copies, so whether this script
# should still exist is now a real question -- for whoever owns the
# demo, not for this lane to decide by deleting it.
#
#   devcheck/sync-trees.sh --check    # verify (no writes)
#   devcheck/sync-trees.sh            # show what would change
#   devcheck/sync-trees.sh --write    # copy from the demo, remanifest
#
# The demo directory is optional: on a box that has only this repo,
# --check still verifies the copies against the manifest, which
# catches a corrupted or half-synced tree.

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)
DEMO=${DEMO:-$ROOT/../zfs-rebase-theory/decide-demo}
MANIFEST=trees/MANIFEST

mode=show
case "$1" in
--check)	mode=check ;;
--write)	mode=write ;;
"")		mode=show ;;
*)		echo "usage: $0 [--check|--write]"; exit 2 ;;
esac

fail=0

# --- 1. the copies match the manifest
if [ ! -f "$MANIFEST" ]; then
	if [ "$mode" = check ]; then
		echo "MISSING: $MANIFEST -- run $0 --write"
		exit 1
	fi
else
	n=0
	while read -r sum name; do
		[ -z "$sum" ] && continue
		case "$sum" in \#*) continue ;; esac
		if [ ! -f "$name" ]; then
			echo "MISSING: $name (in manifest, not on disk)"
			fail=1
			continue
		fi
		got=$(cksum < "$name" | awk '{print $1 "-" $2}')
		if [ "$got" != "$sum" ]; then
			echo "CHANGED: $name does not match the manifest"
			fail=1
		fi
		n=$((n + 1))
	done < "$MANIFEST"
	[ "$fail" -eq 0 ] && echo "manifest OK ($n fixture(s) unchanged)"
fi

# --- 2. the copies match the demo, when the demo is here
if [ -d "$DEMO/examples" ]; then
	diffs=0
	for src in "$DEMO"/examples/*.tree "$DEMO"/examples/*/*.tree; do
		[ -e "$src" ] || continue
		rel=${src#"$DEMO"/examples/}
		dst=trees/$rel
		if [ ! -f "$dst" ]; then
			echo "NEW in demo: $rel"
			diffs=$((diffs + 1))
			[ "$mode" = write ] && mkdir -p "$(dirname "$dst")" &&
			    cp "$src" "$dst"
			continue
		fi
		if ! cmp -s "$src" "$dst"; then
			echo "DIFFERS: $rel"
			diffs=$((diffs + 1))
			[ "$mode" = write ] && cp "$src" "$dst"
		fi
	done
	# Fixtures that exist only on this side. Announced on every run
	# and deliberately NOT a failure: the op3 and op6 worked examples
	# are authored here because this is the repository this lane can
	# commit to, and the copy owed to the demo is somebody else's
	# write. Loud and un-forgettable is the right treatment for an
	# owed copy; red is not, because nothing here can turn it green.
	only=0
	for dst in trees/*.tree trees/*/*.tree; do
		[ -e "$dst" ] || continue
		rel=${dst#trees/}
		[ -f "$DEMO/examples/$rel" ] && continue
		echo "HARNESS-ONLY: $rel is not in the demo corpus"
		only=$((only + 1))
	done
	if [ "$only" -gt 0 ]; then
		echo "$only fixture(s) authored here, copy owed to the demo"
	fi

	if [ "$diffs" -eq 0 ] && [ "$only" -eq 0 ]; then
		echo "demo OK (corpus is byte-identical to the demo's)"
	elif [ "$diffs" -eq 0 ]; then
		echo "demo OK for the shared fixtures; see HARNESS-ONLY above"
	elif [ "$mode" = check ]; then
		echo "the two copies of the corpus have drifted"
		fail=1
	elif [ "$mode" = write ]; then
		echo "copied $diffs fixture(s) from the demo"
	else
		echo "$diffs fixture(s) would be copied; rerun with --write"
	fi
else
	echo "SKIP: no demo corpus at $DEMO/examples"
fi

# --- 3. remanifest
if [ "$mode" = write ]; then
	{
		echo "# cksum of every .tree fixture, written by" \
		    "devcheck/sync-trees.sh."
		echo "# Verified by devcheck/treecheck.sh. Regenerate" \
		    "with --write after a"
		echo "# deliberate corpus change, never to silence a" \
		    "CHANGED line you did"
		echo "# not expect."
		for f in trees/*.tree trees/*/*.tree; do
			[ -e "$f" ] || continue
			echo "$(cksum < "$f" | awk '{print $1 "-" $2}') $f"
		done
	} > "$MANIFEST"
	echo "wrote $MANIFEST"
fi

exit "$fail"
