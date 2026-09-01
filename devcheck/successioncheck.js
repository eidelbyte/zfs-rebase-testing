/*
 * successioncheck.js -- run succession.js against the op6 fixtures'
 * hand-worked outcomes.
 *
 * WHAT IS AND IS NOT UNDER TEST.  The deltas below are transcribed
 * from each fixture's WORKED OUTCOME block; they are not derived
 * here, because attributing a lost name to the right fragment of a
 * split pool is a pairing judgement and reimplementing pairing would
 * make this a worse copy of the engine rather than a second opinion.
 * What is under test is the RULE that consumes them: two-pass
 * realization with both guards, delta routing, and the one-move rule
 * restated on deltas.
 *
 * onto's side is read from the fixture itself, so the holder-star
 * lookups and the type filter run against real parsed trees rather
 * than against a table someone typed.
 *
 * ORDER INDEPENDENCE IS CHECKED, not assumed.  Every realization case
 * runs twice, with the output pools handed over forwards and
 * backwards, and both must agree.  That is the property the
 * claimed-guard pair exists to assert and the one a single-pass
 * implementation fails.
 *
 *   jsc jsc-shim.js /path/to/successioncheck.js -- <file.tree>...
 */
"use strict";

var engine = require("./engine.js");

var SUCC = (typeof succPath !== "undefined") ? succPath :
    "/Users/miri/freebsd-development/zrt-tree-suite/succession.js";
load(SUCC);
var succ = module.exports;

var M = "materialize";

/*
 * Per fixture: the output pools with their decided names and lost
 * sets, and the realization answer worked by hand.  "pools" are named
 * by their lowest-sorted decided name, which is how the engine
 * identifies them.
 */
var CASES = {
	"op6-wholesale-quiet.tree": {
		pools: [{ name: "aa", names: ["/shared/aa", "/shared/bb"],
		    lost: ["/shared/x", "/shared/y"], type: "file" }],
		realize: { aa: "10-100" }
	},
	"op6-singleton-vs-split.tree": {
		pools: [
		    { name: "aa", names: ["/shared/aa", "/shared/y"],
		      lost: ["/shared/x"], type: "file" },
		    { name: "z", names: ["/shared/z"], lost: [], type: "file" }],
		realize: { aa: "10-100", z: "30-200" }
	},
	"op6-fragment-multi.tree": {
		pools: [
		    { name: "aa", names: ["/shared/aa", "/shared/bb"],
		      lost: ["/shared/x", "/shared/y"], type: "file" },
		    { name: "z", names: ["/shared/z"], lost: [], type: "file" }],
		realize: { aa: "10-100", z: "30-200" },
		route: { aa: "10-100" }
	},
	"op6-straddle-split.tree": {
		pools: [{ name: "aa",
		    names: ["/shared/aa", "/shared/cc", "/shared/y"],
		    lost: ["/shared/x", "/shared/z"], type: "file" }],
		/*
		 * No realization expectation: consumer 2's straddle
		 * conflict fires first and the region quarantines, so
		 * realization never runs on this pool.  The routing
		 * verdict is the assertion.
		 */
		straddle: ["aa"]
	},
	"op6-claimed-guard.tree": {
		pools: [
		    { name: "aa", names: ["/shared/aa"],
		      lost: ["/shared/x"], type: "file" },
		    { name: "y", names: ["/shared/y"], lost: [], type: "file" }],
		realize: { aa: M, y: "10-100" }
	},
	"op6-claimed-guard-mirror.tree": {
		pools: [
		    { name: "a", names: ["/shared/a"], lost: [], type: "file" },
		    { name: "zz", names: ["/shared/zz"],
		      lost: ["/shared/x"], type: "file" }],
		realize: { a: "10-100", zz: M }
	},
	"op6-type-guard.tree": {
		pools: [{ name: "aa", names: ["/shared/aa", "/shared/bb"],
		    lost: ["/shared/x", "/shared/y"], type: "file" }],
		realize: { aa: "10-100" },
		route: { aa: "10-100" }
	},
	"op6-one-move-deltas.tree": {
		onemove: { onto: { lost: ["/shared/x"], gained: ["/shared/p"] },
		    offof: { lost: ["/shared/x"], gained: ["/shared/q"] },
		    conflict: true }
	},
	"op6-identical-merge-deltas.tree": {
		onemove: { onto: { lost: ["/shared/x"], gained: ["/shared/p"] },
		    offof: { lost: ["/shared/x"], gained: ["/shared/p"] },
		    conflict: false }
	},
	"op6-attachment-no-loss.tree": {
		pools: [{ name: "x", names: ["/shared/x", "/shared/z"],
		    lost: [], type: "file" }],
		realize: { x: "10-100" }
	}
};

function basename(p) {
	var i = p.lastIndexOf("/");
	return (i < 0) ? p : p.slice(i + 1);
}

function show(m) {
	var out = [], k, keys = [];

	for (k in m) if (Object.prototype.hasOwnProperty.call(m, k)) keys.push(k);
	keys.sort();
	for (k = 0; k < keys.length; k++) out.push(keys[k] + "=" + m[keys[k]]);
	return (out.join(" "));
}

var paths = (typeof arguments !== "undefined") ? arguments : [];
var i, agree = 0, differ = 0;

for (i = 0; i < paths.length; i++) {
	var name = basename(paths[i]);
	if (!Object.prototype.hasOwnProperty.call(CASES, name)) continue;

	var spec = engine.parseSpec(readFile(paths[i]));
	if (spec.errors.length > 0) {
		print(name + ": DOES NOT PARSE");
		differ++;
		continue;
	}

	var c = CASES[name];
	var onto = succ.ontoView(spec.trees.onto);
	var bad = false, note = [];

	if (c.realize !== undefined) {
		var fwd = succ.realize(onto, c.pools);
		var rev = succ.realize(onto, c.pools.slice().reverse());

		if (show(fwd) !== show(c.realize)) {
			bad = true;
			print("DIFFER  " + name + "  realization");
			print("        by hand: " + show(c.realize));
			print("        by code: " + show(fwd));
		} else if (show(fwd) !== show(rev)) {
			bad = true;
			print("DIFFER  " + name + "  ORDER DEPENDENT");
			print("        forwards: " + show(fwd));
			print("        backwards: " + show(rev));
			print("        the two-pass rule exists to make these");
			print("        identical; a one-pass loop fails here");
		} else {
			note.push("realize " + show(fwd) + " (order-independent)");
		}
	}

	if (c.route !== undefined && !bad) {
		var k, r;

		for (k = 0; k < c.pools.length; k++) {
			if (!Object.prototype.hasOwnProperty.call(c.route,
			    c.pools[k].name)) continue;
			r = succ.routeDelta(onto, c.pools[k]);
			if (r.route !== c.route[c.pools[k].name]) {
				bad = true;
				print("DIFFER  " + name + "  delta routing for " +
				    c.pools[k].name);
				print("        by hand: " +
				    c.route[c.pools[k].name]);
				print("        by code: " +
				    (r.straddle ? "straddle" : r.route));
			}
		}
		if (!bad) note.push("delta routes wholesale");
	}

	if (c.straddle !== undefined && !bad) {
		for (k = 0; k < c.pools.length; k++) {
			if (c.straddle.indexOf(c.pools[k].name) < 0) continue;
			r = succ.routeDelta(onto, c.pools[k]);
			if (!r.straddle) {
				bad = true;
				print("DIFFER  " + name + "  expected a straddle");
				print("        by code: routes to " + r.route);
			}
		}
		if (!bad) note.push("lost set straddles, conflict");
	}

	if (c.onemove !== undefined && !bad) {
		var v = succ.oneMove(c.onemove.onto, c.onemove.offof);

		if (v.conflict !== c.onemove.conflict) {
			bad = true;
			print("DIFFER  " + name + "  one-move rule");
			print("        by hand: conflict=" + c.onemove.conflict);
			print("        by code: conflict=" + v.conflict);
		} else {
			note.push("one-move conflict=" + v.conflict);
		}
	}

	if (bad) {
		differ++;
		continue;
	}
	agree++;
	print("agree   " + name + "  " + note.join("; "));
}

print("");
print("successioncheck: " + agree + " agree, " + differ + " differ");
if (differ > 0) {
	print("A disagreement is a finding. The deltas are hand-worked");
	print("inputs and the rule is what is under test; decide which of");
	print("the two is wrong before editing either.");
}
