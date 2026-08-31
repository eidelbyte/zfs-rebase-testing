/*
 * emitcounts.js -- run emit.js over the corpus and print per-fixture
 * edit counts by verb.
 *
 * Coarse counts are the comparison available before a manifest can
 * carry a full edit list, and they are the thing the two independent
 * emitters compare first.  The point is that neither side has read
 * the other, so agreement is evidence and disagreement means one of
 * us has misread Section 12.
 *
 * Run from the decide-demo directory, which is where engine.js and
 * the shim live:
 *
 *   jsc jsc-shim.js /path/to/emitcounts.js -- <file.tree>...
 *
 * EMIT_JS may point at the compiler; it defaults to the copy beside
 * this script.
 */
"use strict";

var engine = require("./engine.js");

/*
 * emit.js lives in the harness repo rather than beside engine.js,
 * because the harness repo is version controlled and the theory
 * directory is not.  Loaded by absolute path for that reason.
 */
var EMIT = (typeof emitPath !== "undefined") ? emitPath :
    "/Users/miri/freebsd-development/zrt-tree-suite/emit.js";
load(EMIT);
var emit = module.exports;

function basename(p) {
	var i = p.lastIndexOf("/");
	return (i < 0) ? p : p.slice(i + 1);
}

var paths = (typeof arguments !== "undefined") ? arguments : [];
var totals = { unlink: 0, link: 0, rename: 0, write: 0, materialize: 0 };
var i, v;
var verbs = ["unlink", "link", "rename", "write", "materialize"];

for (i = 0; i < paths.length; i++) {
	var spec = engine.parseSpec(readFile(paths[i]));

	if (spec.errors.length > 0) {
		print(basename(paths[i]) + ": DOES NOT PARSE");
		continue;
	}

	var res = engine.decide(spec);
	var out = emit.compileEdits(spec, res);
	var c = emit.countByVerb(out.edits);
	var line = [];

	for (v = 0; v < verbs.length; v++) {
		line.push(verbs[v] + "=" + c[verbs[v]]);
		totals[verbs[v]] += c[verbs[v]];
	}
	print(basename(paths[i]) + "  " + line.join(" ") +
	    "  total=" + out.edits.length +
	    (res.conflicts.length > 0 ?
	    "  (" + res.conflicts.length + " conflict(s), " +
	    res.quarantine.length + " held)" : "  (clean)"));

	for (v = 0; v < out.warnings.length; v++)
		print("    WARNING: " + out.warnings[v]);

	var problems = emit.verifyEdits(spec, res, out.edits);
	for (v = 0; v < problems.length; v++)
		print("    " + problems[v]);
}

var tl = [];
for (v = 0; v < verbs.length; v++) tl.push(verbs[v] + "=" + totals[verbs[v]]);
print("TOTAL  " + tl.join(" "));
