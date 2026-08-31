/*
 * emit.js -- Section 12 emission, first half: compiling a decision
 * into an edit list.
 *
 * A SECOND OPINION, DELIBERATELY UNINFORMED.  Every decide pass has
 * had two independent implementations, and agreement between them is
 * the only evidence either one is right.  Emission has had one.  This
 * is the other, and it was written WITHOUT reading the kernel's
 * emitter -- from Section 12 of the theory, section 8.1 of the
 * manifest contract, and the decision record's public shape.  If it
 * had been written by reading the C, agreement would prove only that
 * one misreading was copied twice.
 *
 * WHAT COMPILATION IS.  The output tree begins as a clone of ONTO, so
 * an edit list is the difference between onto's namespace and the
 * decided one, expressed in Definition 12.1's vocabulary and
 * respecting identity.  Nothing here chooses which dnode a name gets:
 * the decision already said which names survive and how they group,
 * and reuse is a consequence of that grouping rather than a choice
 * made now.
 *
 * The cases, from the specification:
 *
 *   /a moves to /b, same pool          rename           (a move)
 *   /a moves to /b, different pools    unlink + link    (delete+add)
 *   /a stays put, different pool       unlink + link    (delete+add)
 *   /a stays put, same pool            nothing
 *   a pool dies                        unlink per name
 *
 * The fourth line is the important one: on a real filesystem it is
 * almost everything, and a compiler that emits work for it is wrong
 * rather than merely slow.
 *
 * SCOPE.  Compilation only.  Not the topological order of Proposition
 * 12.2, not scratch renames for rotations, not the manifest.  The
 * edits come out in a deterministic but arbitrary order; ordering is
 * a separate problem and a separate issue.
 */
"use strict";

function strCmp(a, b) { return a < b ? -1 : (a > b ? 1 : 0); }
function sortedNames(list) { return list.slice().sort(strCmp); }

function hasOwn(o, k) {
	return Object.prototype.hasOwnProperty.call(o, k);
}

/* Set difference and intersection over sorted name lists. */
function without(names, drop) {
	var out = [], i;

	for (i = 0; i < names.length; i++)
		if (!hasOwn(drop, names[i])) out.push(names[i]);
	return (out);
}

/*
 * Which name a new link or a write should hang off.
 *
 * Prefer a KEPT name -- one the clone already holds -- because it
 * exists before the plan starts and so depends on nothing.  Anchoring
 * on a name the plan itself creates is equally correct and strictly
 * worse: it invents a dependency that ordering then has to carry.
 * Only when the pool keeps no name at all does the lowest-sorted
 * decided name serve, and that case is reachable -- a materialized
 * pool keeps nothing by construction, so every name after its first
 * arrives here.
 */
function anchorName(kept, decided) {
	if (kept.length > 0) return (kept[0]);
	return (decided.length > 0 ? decided[0] : null);
}

function asSet(names) {
	var s = {}, i;

	for (i = 0; i < names.length; i++) s[names[i]] = true;
	return (s);
}

/*
 * Names a quarantined component holds.  Definition 12.4: such a
 * component emits NOTHING and onto's state stays in place beneath it.
 * That makes held names invisible in both directions -- they are not
 * gained, and, just as importantly, they are never unlinked because
 * they failed to survive.  A compiler that forgets the second half
 * deletes the very state quarantine exists to preserve.
 */
function heldNameSet(res) {
	var held = {};
	var quarantined = {};
	var i, j, names;

	for (i = 0; i < res.quarantine.length; i++)
		quarantined[res.quarantine[i].componentId] = true;

	for (i = 0; i < res.components.length; i++) {
		if (quarantined[res.components[i].id] !== true) continue;
		names = res.components[i].names;
		for (j = 0; j < names.length; j++) held[names[j]] = true;
	}
	return (held);
}

/*
 * Compile a decision into edits against a clone of onto.
 *
 * Returns { edits, warnings }.  A warning is a statement the decision
 * made that this compiler could not express; there should never be
 * one, so any warning is a finding rather than a diagnostic.
 */
function compileEdits(spec, res) {
	var onto = spec.trees.onto;
	var edits = [];
	var warnings = [];
	var held = heldNameSet(res);
	var cloneNamesOf = {};
	var ontoPoolOf = {};
	var claimed = {};
	var i, j, p;

	/*
	 * The clone's starting namespace: onto's, minus anything a
	 * quarantined component holds.
	 */
	for (i = 0; i < onto.pools.length; i++) {
		p = onto.pools[i];
		ontoPoolOf[p.key] = p;
		cloneNamesOf[p.key] = without(p.names, held);
	}

	/* --- Output pools: what the decision wants to exist. --- */
	for (i = 0; i < res.outputPools.length; i++) {
		var op = res.outputPools[i];

		/* A held pool emits nothing at all (Definition 12.4). */
		if (hasOwn(held, op.names[0])) continue;

		var key = op.cloneKey;
		var cloneNames = (key !== null && hasOwn(cloneNamesOf, key)) ?
		    cloneNamesOf[key] : [];
		var outNames = sortedNames(op.names);
		var outSet = asSet(outNames);
		var cloneSet = asSet(cloneNames);

		var lost = without(cloneNames, outSet);
		var gained = without(outNames, cloneSet);
		var kept = without(outNames, asSet(gained));

		if (key !== null) {
			if (claimed[key] === true) {
				warnings.push("onto pool " + key + " is reused " +
				    "by two output pools");
			}
			claimed[key] = true;
		}

		if (op.realization === "materialized") {
			/*
			 * Section 8.1: an applier cannot create an
			 * anonymous node, so materialize carries the
			 * pool's FIRST name and the rest are ordinary
			 * links.  Nothing is lost or kept -- there is no
			 * clone-side material to lose.
			 */
			if (gained.length === 0) {
				warnings.push("materialized pool " + op.id +
				    " has no name to create");
				continue;
			}
			edits.push({ verb: "materialize", path: gained[0],
			    pool: op.id });
			for (j = 1; j < gained.length; j++) {
				edits.push({ verb: "link", path: gained[j],
				    from: anchorName([], gained),
				    pool: op.id });
			}
			continue;
		}

		/*
		 * An onto dnode is reused.  A lost name paired with a
		 * gained one is a MOVE and must be a rename: unlink
		 * first would drop the pool's last clone-side link and
		 * free the dnode the link was going to re-create (the
		 * last-link arrow, and the story that motivates it).
		 * Pairing in sorted order is arbitrary but has to be
		 * deterministic, since two emitters must agree.
		 */
		var npair = Math.min(lost.length, gained.length);
		for (j = 0; j < npair; j++) {
			edits.push({ verb: "rename", path: lost[j],
			    to: gained[j], pool: op.id });
		}
		for (j = npair; j < lost.length; j++)
			edits.push({ verb: "unlink", path: lost[j] });

		/*
		 * Any further gained name is an added hard link.  It
		 * hangs off a name the pool already has on the clone --
		 * a kept one, or the target of the first rename.
		 */
		var anchor = anchorName(kept, outNames);
		for (j = npair; j < gained.length; j++) {
			if (anchor === null) {
				warnings.push("pool " + op.id + " gains " +
				    gained[j] + " with no clone-side name " +
				    "to link from");
				continue;
			}
			edits.push({ verb: "link", path: gained[j],
			    from: anchor, pool: op.id });
		}

		/*
		 * Content.  The dnode is onto's, so bytes are already
		 * right unless the decision took them from somewhere
		 * else.  Comparing the chosen content against what the
		 * clone already holds is what keeps the quiet case
		 * quiet.
		 */
		var ontoPool = hasOwn(ontoPoolOf, key) ? ontoPoolOf[key] :
		    null;
		if (ontoPool !== null && op.token !== ontoPool.token) {
			/*
			 * Any name of the pool reaches its dnode, so the
			 * write may carry any of them -- and the same
			 * argument as the link anchor applies: naming a
			 * name the plan creates makes the write wait for
			 * the edit that creates it, where a kept name
			 * waits for nothing.
			 */
			edits.push({ verb: "write",
			    path: anchorName(kept, outNames), pool: op.id });
		}
	}

	/*
	 * --- Onto pools no output pool reuses: their material dies. ---
	 * Every name they still hold on the clone is unlinked.  A pool
	 * whose names were all taken over by other pools has nothing
	 * left here and contributes nothing.
	 */
	for (i = 0; i < onto.pools.length; i++) {
		p = onto.pools[i];
		if (claimed[p.key] === true) continue;

		/*
		 * Every remaining name goes, with no exception for one
		 * that survives on a DIFFERENT pool: that case is
		 * "stays at /a but changes pool", which is delete and
		 * add, and this is the delete half.  Held names were
		 * already removed from the clone set above, so there is
		 * nothing here to suppress.
		 */
		var doomed = cloneNamesOf[p.key];
		for (j = 0; j < doomed.length; j++)
			edits.push({ verb: "unlink", path: doomed[j] });
	}

	return ({ edits: edits, warnings: warnings });
}

/*
 * ------------------------------------------------------------------
 * Checking the compiler against itself
 * ------------------------------------------------------------------
 *
 * Counts are only worth comparing if the list they count is right, so
 * this applies the edits to a model of the clone and asks whether the
 * result IS the decided tree -- every name present, on the identity
 * the decision assigned it.  That is the property compilation exists
 * to have, and it can be checked here with no kernel and no box.
 *
 * Edits are applied in whatever order works: repeatedly take any edit
 * whose precondition currently holds.  That is not the topological
 * order of Proposition 12.2 and is not trying to be -- but if the
 * loop STALLS with edits remaining, the list has a rotation in it,
 * which is exactly the case Proposition 12.2 breaks with a scratch
 * rename.  Since scratch names are out of scope here, a stall is a
 * reported fact rather than a failure: it says this fixture needs the
 * ordering pass, and names which edits are waiting on each other.
 */
/* Does any live name sit strictly beneath this path? */
function hasDescendant(state, path) {
	var prefix = (path === "/") ? "/" : path + "/";
	var name;

	for (name in state) {
		if (!hasOwn(state, name)) continue;
		if (name !== path && name.indexOf(prefix) === 0)
			return (true);
	}
	return (false);
}

function verifyEdits(spec, res, edits) {
	var onto = spec.trees.onto;
	var held = heldNameSet(res);
	var state = {};
	var want = {};
	/* What each identity HOLDS, so a dropped write is visible.
	 * Namespace and identity alone cannot see one: the same names
	 * sit on the same dnodes either way, and only the bytes differ. */
	var content = {};
	var wantContent = {};
	var problems = [];
	var pending = edits.slice();
	var i, j, p;

	/* The clone: onto's names, each on onto's dnode. */
	for (i = 0; i < onto.pools.length; i++) {
		p = onto.pools[i];
		for (j = 0; j < p.names.length; j++) {
			if (hasOwn(held, p.names[j])) continue;
			state[p.names[j]] = "onto:" + p.key;
		}
		content["onto:" + p.key] = p.token;
	}

	/* Where the decision says every name should end up. */
	for (i = 0; i < res.outputPools.length; i++) {
		var op = res.outputPools[i];

		if (hasOwn(held, op.names[0])) continue;
		var ident = op.cloneKey !== null ? "onto:" + op.cloneKey :
		    "fresh:" + op.id;
		for (j = 0; j < op.names.length; j++) want[op.names[j]] = ident;
		wantContent[ident] = op.token;
	}

	var progress = true;
	while (progress && pending.length > 0) {
		progress = false;
		for (i = 0; i < pending.length; i++) {
			var e = pending[i];
			var ok = false;

			if (e.verb === "unlink") {
				/*
				 * Children first: a directory's entry
				 * cannot go while anything still lives
				 * beneath it.  Checked on the namespace
				 * rather than on types, since a file
				 * never has descendants anyway.
				 */
				if (hasOwn(state, e.path) &&
				    !hasDescendant(state, e.path)) {
					delete state[e.path];
					ok = true;
				}
			} else if (e.verb === "rename") {
				if (hasOwn(state, e.path) && !hasOwn(state, e.to)) {
					state[e.to] = state[e.path];
					delete state[e.path];
					ok = true;
				}
			} else if (e.verb === "link") {
				if (hasOwn(state, e.from) && !hasOwn(state, e.path)) {
					state[e.path] = state[e.from];
					ok = true;
				}
			} else if (e.verb === "materialize") {
				if (!hasOwn(state, e.path)) {
					state[e.path] = "fresh:" + e.pool;
					content["fresh:" + e.pool] =
					    poolToken(res, e.pool);
					ok = true;
				}
			} else if (e.verb === "write") {
				if (hasOwn(state, e.path)) {
					content[state[e.path]] =
					    poolToken(res, e.pool);
					ok = true;
				}
			}

			if (ok) {
				pending.splice(i, 1);
				i--;
				progress = true;
			}
		}
	}

	if (pending.length > 0) {
		var stuck = [];
		for (i = 0; i < pending.length; i++) {
			stuck.push(pending[i].verb + " " + pending[i].path +
			    (pending[i].to ? " -> " + pending[i].to : ""));
		}
		problems.push("no order applies " + pending.length +
		    " edit(s), a rotation needing a scratch name: " +
		    stuck.join("; "));
	}

	/* The namespace, and the identity under every name. */
	var name;
	for (name in want) {
		if (!hasOwn(want, name)) continue;
		if (!hasOwn(state, name)) {
			problems.push(name + " should exist and does not");
		} else if (state[name] !== want[name]) {
			problems.push(name + " should be on " + want[name] +
			    " and is on " + state[name]);
		}
	}
	for (name in state) {
		if (!hasOwn(state, name)) continue;
		if (!hasOwn(want, name))
			problems.push(name + " survived and should not have");
	}

	var ident2;
	for (ident2 in wantContent) {
		if (!hasOwn(wantContent, ident2)) continue;
		if (content[ident2] !== wantContent[ident2]) {
			problems.push(ident2 + " should hold '" +
			    wantContent[ident2] + "' and holds '" +
			    content[ident2] + "'");
		}
	}

	return (problems);
}

/* The content an output pool decided on. */
function poolToken(res, poolId) {
	var i;

	for (i = 0; i < res.outputPools.length; i++)
		if (res.outputPools[i].id === poolId)
			return (res.outputPools[i].token);
	return (undefined);
}

/* Counts by verb, which is the comparison available before the
 * manifest can carry a full edit list. */
function countByVerb(edits) {
	var counts = { unlink: 0, link: 0, rename: 0, write: 0,
	    materialize: 0 };
	var i;

	for (i = 0; i < edits.length; i++) counts[edits[i].verb]++;
	return (counts);
}

module.exports = { compileEdits: compileEdits, countByVerb: countByVerb,
    verifyEdits: verifyEdits };
