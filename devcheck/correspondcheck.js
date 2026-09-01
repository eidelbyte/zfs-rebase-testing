/*
 * correspondcheck.js -- run correspond.js over the op3 corpus and
 * check the believed moves against what the fixtures were worked to
 * by hand.
 *
 * WHAT IS BEING CHECKED.  Each op3 fixture's comment reasons its way
 * to a set of believed directory moves before any code ran.  That
 * reasoning is the artifact wave 2 is gated on, and reasoning that
 * has never been executed is a hypothesis.  This executes it.
 *
 * A disagreement is a FINDING, not a test failure: it means the prose
 * and the code read Section 16 differently, and one of them is wrong.
 * Which one is a question for a person.  The runner therefore prints
 * both sides of every disagreement rather than just a count.
 *
 * The expectations below are transcribed from the fixtures, on the
 * base-to-off-of and base-to-onto faces separately.  Section 16 never
 * compares onto with off-of, so there is no third face.
 *
 * Run from the decide-demo directory, where engine.js and the shim
 * live:
 *
 *   jsc jsc-shim.js /path/to/correspondcheck.js -- <file.tree>...
 *
 * CORRESPOND_JS may point at the implementation; it defaults to the
 * copy in the harness repo.
 */
"use strict";

var engine = require("./engine.js");

var CORRESPOND = (typeof correspondPath !== "undefined") ? correspondPath :
    "/Users/miri/freebsd-development/zrt-tree-suite/correspond.js";
load(CORRESPOND);
var cor = module.exports;

/*
 * Hand-worked expectations, one entry per fixture, transcribed from
 * the reasoning in each .tree file.  A directory corresponded by red
 * or green needs no belief and must produce none, so most entries are
 * empty and that is the point: belief is for the case red cannot see.
 */
var EXPECT = {
	"op3-carried-subtree.tree": { onto: [], offof: [] },
	"op3-carried-child-rename.tree": { onto: [], offof: [] },
	"op3-cross-rename-childop.tree": { onto: [], offof: [] },
	"op3-hardpair-under-rename.tree": { onto: [], offof: [] },
	"op3-recreated-move-edited-stayer.tree":
	    { onto: [], offof: ["/old -> /new"] },
	"op3-dir-merge-fanin.tree":
	    { onto: [], offof: ["/a -> /d", "/c -> /d"] },
	"op3-dup-content-ambig.tree": { onto: [], offof: [] },
	"op3-emigrant.tree": { onto: [], offof: [] },
	"op3-empty-dir.tree": { onto: [], offof: ["/full -> /filled"] },
	"op3-nested-recreated.tree": { onto: [], offof: [] },
	"op3-severed-cross-parent.tree": { onto: [], offof: [] },
	"op3-dir-yellow-abstain.tree": { onto: [], offof: ["/proj -> /work"] },
	"op3-nested-believed.tree":
	    { onto: [], offof: ["/top -> /peak", "/top/inner -> /peak/core"] },
	"op3-compose-through-base.tree":
	    { onto: ["/box -> /crate"], offof: [] }
};

function basename(p) {
	var i = p.lastIndexOf("/");
	return (i < 0) ? p : p.slice(i + 1);
}

function fmt(face) {
	var out = [], i;

	for (i = 0; i < face.moves.length; i++)
		out.push(face.moves[i].from + " -> " + face.moves[i].to);
	return (out);
}

function same(a, b) {
	var i;

	if (a.length !== b.length) return (false);
	for (i = 0; i < a.length; i++) if (a[i] !== b[i]) return (false);
	return (true);
}

var paths = (typeof arguments !== "undefined") ? arguments : [];
var i, agree = 0, differ = 0, skipped = 0;

for (i = 0; i < paths.length; i++) {
	var name = basename(paths[i]);
	var spec = engine.parseSpec(readFile(paths[i]));
	var res, got, want, faces, f, bad;

	if (spec.errors.length > 0) {
		print(name + ": DOES NOT PARSE");
		differ++;
		continue;
	}
	if (!Object.prototype.hasOwnProperty.call(EXPECT, name)) {
		skipped++;
		continue;
	}
	res = cor.correspond(spec);
	want = EXPECT[name];
	faces = ["onto", "offof"];
	bad = false;
	for (f = 0; f < faces.length; f++) {
		got = fmt(res[faces[f]]);
		if (same(got, want[faces[f]])) continue;
		bad = true;
		print("DIFFER  " + name + "  face base-to-" + faces[f]);
		print("        by hand: " +
		    (want[faces[f]].length ? want[faces[f]].join(", ") :
		    "(no believed moves)"));
		print("        by code: " +
		    (got.length ? got.join(", ") : "(no believed moves)"));
		if (res[faces[f]].ambiguous.length > 0) {
			print("        ambiguous target for: " +
			    res[faces[f]].ambiguous.join(", "));
		}
	}
	if (bad) {
		differ++;
		continue;
	}
	agree++;
	var mv = fmt(res.offof);
	var amb = res.offof.ambiguous;

	print("agree   " + name + "  candidates=" + res.offof.candidates +
	    "  believed=" + (mv.length ? mv.join(", ") : "none") +
	    (amb.length ? "  ambiguous=" + amb.join(",") : ""));
}

print("");
print("correspondcheck: " + agree + " agree, " + differ + " differ" +
    (skipped ? ", " + skipped + " not in the table" : ""));
if (differ > 0) {
	print("A disagreement is a finding. Read both lines above and");
	print("decide which reading of Section 16 is wrong before editing");
	print("either side to match the other.");
}
