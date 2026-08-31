/*
 * tree-canon.js -- print the reference parser's canonical view of a
 * .tree file, in exactly the format devcheck/treedump.c prints.
 *
 * This exists to be diffed.  The .tree corpus is read by two
 * independent parsers -- the reference one in engine.js, which the
 * demo and the gold generator use, and the C one in rt_tree_parse.c,
 * which the kernel harness uses -- and a fixture that means two
 * different things to two readers is worse than no fixture at all.
 * devcheck/treecheck.sh runs both over the whole corpus and diffs.
 *
 * The format is contract between this file and rt_spec_dump().  If
 * one moves the other has to, or the gate stops meaning anything.
 *
 * Run from the decide-demo directory, which is where engine.js and
 * the shim live:
 *
 *   jsc jsc-shim.js tree-canon.js -- <file.tree>...
 */
"use strict";

var engine = require("./engine.js");

var TREES = [
	{ slot: "base", key: "base" },
	{ slot: "onto", key: "onto" },
	{ slot: "offof", key: "offof" }
];

function basename(p) {
	var i = p.lastIndexOf("/");
	return (i < 0) ? p : p.slice(i + 1);
}

function dumpTree(out, label, tree) {
	var i, j;

	out.push("tree " + label);
	for (i = 0; i < tree.pools.length; i++) {
		var pool = tree.pools[i];
		out.push("  pool " + pool.key + " " + pool.type +
		    " token=" + pool.token);
		for (j = 0; j < pool.names.length; j++)
			out.push("    name " + pool.names[j]);
	}
}

function dumpSpec(path, text) {
	var spec = engine.parseSpec(text);
	var out = [];
	var i;

	out.push("spec " + basename(path));
	out.push("title " + spec.title);

	for (i = 0; i < TREES.length; i++)
		dumpTree(out, TREES[i].slot, spec.trees[TREES[i].key]);

	/*
	 * Gold is emitted only when it has content: the reference
	 * parser builds an expected tree from node lines alone, so an
	 * empty block carries no expectation and must print as none.
	 */
	if (spec.expected !== null && spec.expected !== undefined)
		dumpTree(out, "expected", spec.expected);

	for (i = 0; i < spec.expects.length; i++) {
		var ex = spec.expects[i];
		if (ex.kind === "clean")
			out.push("expect clean");
		else
			out.push("expect " + ex.kind + " " + ex.arg);
	}

	for (i = 0; i < spec.errors.length; i++) {
		out.push("error line " + spec.errors[i].line + ": " +
		    spec.errors[i].message);
	}

	return out.join("\n");
}

var paths = (typeof arguments !== "undefined") ? arguments : [];
var i;

for (i = 0; i < paths.length; i++)
	print(dumpSpec(paths[i], readFile(paths[i])));
