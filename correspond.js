/*
 * correspond.js -- Section 16 directory correspondence, independently.
 *
 * WHAT THIS IS.  The theory's settlement of open problem 3 keeps the
 * name universe as full paths and adds a second READING: a name may
 * be read as (context, leaf) inside a corresponded pair of
 * directories, where leaves are unique.  Correspondence comes from
 * red (a rename keeps the dnode), green (same full path), and a
 * derived source -- the fullness rule, under which a recreated
 * directory's surviving children vote its move into existence.
 *
 * This computes that third source: the candidate closure of
 * Proposition 16.4 phase one, and the beliefs of phase two.  It is
 * the new phase and only the new phase; pairing, the four passes and
 * quarantine are not here and are not needed to judge it.
 *
 * WHY IT LIVES HERE.  Same reason emit.js does.  The reference engine
 * is in the theory directory and this repository is the one this lane
 * can commit to, so an independent implementation is written here and
 * loaded by the demo's runner.  (The premise that the theory
 * directory is unversioned is now stale -- the parent repository
 * whitelists it as of 2026-08-31 -- but the split stands on its own:
 * two implementations that share a file are one implementation.)
 *
 * HOW MUCH IT PROVES, HONESTLY.  Agreement between this and the
 * fixtures' hand-worked gold is weaker evidence than agreement
 * between two people's implementations, because the same reader wrote
 * both.  What it catches is the case where careful prose reasoning
 * and careful code disagree, which on this project has already found
 * real defects more than once.  It is a second pass, not a second
 * opinion, and it is worth having for what it is.
 *
 * FACES.  Correspondence is computed on one face at a time, base to
 * onto and base to off-of.  Proposition 16.4: on the cut, believed
 * moves compose through base exactly as derived red does, and raw
 * cross-side belief is never asserted.  So nothing here ever compares
 * onto with off-of.
 */
"use strict";

function parentPath(p) {
	var i;

	if (p === "/") return (null);
	i = p.lastIndexOf("/");
	if (i < 0) return (null);
	return (i === 0) ? "/" : p.slice(0, i);
}

function leafOf(p) {
	var i = p.lastIndexOf("/");
	return (i < 0) ? p : p.slice(i + 1);
}

function hasOwn(o, k) {
	return Object.prototype.hasOwnProperty.call(o, k);
}

/*
 * An index of one tree: which pool holds each directory path, and the
 * entries directly inside each directory.
 *
 * An entry is a (leaf, pool) pair rather than a pool, because a file
 * pool can hold several names in several directories and each of them
 * is separately a child of somewhere.  Definition 1.3 spares us the
 * mirror case: a directory holds exactly one name, so a directory is
 * its path.
 */
function indexTree(tree) {
	var ix = { dirOf: {}, pathOf: {}, entries: {}, pool: {} };
	var i, k, pool, nm, par;

	for (i = 0; i < tree.pools.length; i++) {
		pool = tree.pools[i];
		ix.pool[pool.key] = pool;
		if (pool.type === "dir") {
			ix.dirOf[pool.names[0]] = pool.key;
			ix.pathOf[pool.key] = pool.names[0];
			if (!hasOwn(ix.entries, pool.names[0]))
				ix.entries[pool.names[0]] = [];
		}
	}
	for (i = 0; i < tree.pools.length; i++) {
		pool = tree.pools[i];
		for (k = 0; k < pool.names.length; k++) {
			nm = pool.names[k];
			par = parentPath(nm);
			if (par === null) continue;
			if (!hasOwn(ix.entries, par)) ix.entries[par] = [];
			ix.entries[par].push({ leaf: leafOf(nm),
			    key: pool.key, path: nm });
		}
	}
	return (ix);
}

function entriesUnder(ix, dirPath) {
	return hasOwn(ix.entries, dirPath) ? ix.entries[dirPath] : [];
}

function dirChildren(ix, dirPath) {
	var all = entriesUnder(ix, dirPath), out = [], i;

	for (i = 0; i < all.length; i++)
		if (ix.pool[all[i].key].type === "dir") out.push(all[i]);
	return (out);
}

/* ------------------------------------------------------------------
 * Phase 1: close the candidate relation (Proposition 16.4)
 *
 * A least fixed point over a finite set, judging nothing.  Four
 * generators, and only the fourth needs iterating -- which is the
 * whole reason this is a closure and not a single pass, and the
 * reason the sibling generator must read the CANDIDATE relation
 * rather than the corresponded one.
 * ------------------------------------------------------------------ */

function closeCandidates(A, B) {
	var cands = {};		/* "aPath\tbPath" -> [aPath, bPath] */
	var aPath, bPath, i, k, ents, bpool, changed, keys, pair;

	function add(ap, bp) {
		var id;

		if (ap === undefined || bp === undefined || ap === null ||
		    bp === null)
			return (false);
		if (!hasOwn(A.dirOf, ap) || !hasOwn(B.dirOf, bp))
			return (false);
		id = ap + "\t" + bp;
		if (hasOwn(cands, id)) return (false);
		cands[id] = [ap, bp];
		return (true);
	}

	/* Red directory pairs: the same dnode is a directory in both. */
	for (aPath in A.dirOf) {
		if (!hasOwn(A.dirOf, aPath)) continue;
		bPath = B.pathOf[A.dirOf[aPath]];
		if (bPath !== undefined) add(aPath, bPath);
	}

	/* Green directory pairs: the same full path in both. */
	for (aPath in A.dirOf) {
		if (!hasOwn(A.dirOf, aPath)) continue;
		if (hasOwn(B.dirOf, aPath)) add(aPath, aPath);
	}

	/*
	 * Red anchors: a child's dnode landed somewhere in B, so the
	 * directory it landed in is nominated as a home for its old
	 * parent.  Yellow deliberately does NOT nominate -- a vote is
	 * the pairwise question "is this child yellow to a child of B",
	 * while a nomination is the inverted "what, anywhere, is yellow
	 * to this child", which is a content index over the whole tree
	 * that the lazy pairwise oracle is designed not to be.
	 */
	for (aPath in A.dirOf) {
		if (!hasOwn(A.dirOf, aPath)) continue;
		ents = entriesUnder(A, aPath);
		for (i = 0; i < ents.length; i++) {
			bpool = B.pool[ents[i].key];
			if (bpool === undefined) continue;
			for (k = 0; k < bpool.names.length; k++)
				add(aPath, parentPath(bpool.names[k]));
		}
	}

	/*
	 * Scoped siblings, iterated to a fixed point.  Under a pair
	 * ALREADY PRESENT in the relation, a directory child of A with
	 * no same-leaf counterpart under B is a deletion, one of B's
	 * with no counterpart under A is an addition, and each deletion
	 * nominates each addition.
	 *
	 * The cross product is deliberate: one deletion facing two
	 * additions yields two candidates, not a choice.  Phase one
	 * nominates and judges nothing, which is what makes it monotone.
	 */
	do {
		changed = false;
		keys = [];
		for (i in cands) if (hasOwn(cands, i)) keys.push(i);
		for (i = 0; i < keys.length; i++) {
			pair = cands[keys[i]];
			var aKids = dirChildren(A, pair[0]);
			var bKids = dirChildren(B, pair[1]);
			var aLeaf = {}, bLeaf = {}, j, m;

			for (j = 0; j < aKids.length; j++)
				aLeaf[aKids[j].leaf] = true;
			for (j = 0; j < bKids.length; j++)
				bLeaf[bKids[j].leaf] = true;
			for (j = 0; j < aKids.length; j++) {
				if (hasOwn(bLeaf, aKids[j].leaf)) continue;
				for (m = 0; m < bKids.length; m++) {
					if (hasOwn(aLeaf, bKids[m].leaf))
						continue;
					if (add(aKids[j].path, bKids[m].path))
						changed = true;
				}
			}
		}
	} while (changed);

	return (cands);
}

/* ------------------------------------------------------------------
 * Phase 2: beliefs, bottom-up by height (Proposition 16.4)
 * ------------------------------------------------------------------ */

function heightOf(A, dirPath, memo) {
	var kids, i, h, best;

	if (hasOwn(memo, dirPath)) return (memo[dirPath]);
	memo[dirPath] = 0;		/* guards a malformed cycle */
	kids = dirChildren(A, dirPath);
	best = 0;
	for (i = 0; i < kids.length; i++) {
		h = 1 + heightOf(A, kids[i].path, memo);
		if (h > best) best = h;
	}
	memo[dirPath] = best;
	return (best);
}

/*
 * Which entries under bPath could account for one child of aPath, by
 * the vote triad of Definition 16.3.  An empty list means this
 * candidate does not account for the child at all.
 */
function accountedBy(A, B, ent, bPath, believed) {
	var out = [], bents = entriesUnder(B, bPath), i, bp;
	var apool = A.pool[ent.key];

	for (i = 0; i < bents.length; i++) {
		bp = B.pool[bents[i].key];

		/* Red: the child truly moved, dnode intact. */
		if (bents[i].key === ent.key) {
			out.push(bents[i]);
			continue;
		}

		/*
		 * A directory child votes only through a believed move
		 * of its own, decided at strictly smaller height.  Its
		 * dnode case is the red line above; nothing else about a
		 * directory is evidence, by Remark 16.6.
		 */
		if (apool.type === "dir") {
			if (hasOwn(believed, ent.path) &&
			    believed[ent.path] === bents[i].path)
				out.push(bents[i]);
			continue;
		}

		/*
		 * A name match: the same leaf inside the pair.  This is
		 * relativized green, and it is the row that survives an
		 * edit and a dnode sever at once -- the safe-write case,
		 * where a file is written through a temporary and renamed
		 * over itself.  Consulted before yellow because a leaf is
		 * unique inside a directory and a content class is not.
		 */
		if (bents[i].leaf === ent.leaf) {
			out.push(bents[i]);
			continue;
		}

		/*
		 * Yellow: same bytes elsewhere, non-directories only.
		 * Definition 2.8 compares directories by attributes
		 * alone, so a default-attributed directory is yellow to
		 * nearly every directory and its vote asserts nothing.
		 */
		if (bp.type !== "dir" && apool.type !== "dir" &&
		    bp.token !== "" && bp.token === apool.token)
			out.push(bents[i]);
	}
	return (out);
}

/* A system of distinct representatives, by backtracking. Sets are tiny. */
function oneToOne(choices) {
	var used = {};

	function go(i) {
		var j, id;

		if (i === choices.length) return (true);
		for (j = 0; j < choices[i].length; j++) {
			id = choices[i][j].path;
			if (hasOwn(used, id)) continue;
			used[id] = true;
			if (go(i + 1)) return (true);
			delete used[id];
		}
		return (false);
	}
	return (go(0));
}

function believeMoves(A, B, cands) {
	var byA = {}, believed = {}, memo = {}, order = [], id, i;

	for (id in cands) {
		if (!hasOwn(cands, id)) continue;
		if (!hasOwn(byA, cands[id][0])) byA[cands[id][0]] = [];
		byA[cands[id][0]].push(cands[id][1]);
	}
	for (id in A.dirOf) if (hasOwn(A.dirOf, id)) order.push(id);
	order.sort(function (x, y) {
		var hx = heightOf(A, x, memo), hy = heightOf(A, y, memo);

		if (hx !== hy) return (hx - hy);
		return (x < y ? -1 : (x > y ? 1 : 0));
	});

	for (i = 0; i < order.length; i++) {
		var aPath = order[i];
		var targets = hasOwn(byA, aPath) ? byA[aPath].slice() : [];
		var ents = entriesUnder(A, aPath);
		var winners = [], t, e;

		/*
		 * Red and green already correspond a directory outright
		 * (Definition 16.1).  Belief exists for the case red
		 * cannot see -- a directory rebuilt rather than renamed
		 * -- so a directory whose dnode or whose path survived
		 * needs no vote and gets none.
		 */
		if (hasOwn(B.pathOf, A.dirOf[aPath]) || hasOwn(B.dirOf, aPath))
			continue;

		targets.sort();
		for (t = 0; t < targets.length; t++) {
			var bPath = targets[t];
			var voters = 0, ok = true, choices = [];

			for (e = 0; e < ents.length; e++) {
				var here = accountedBy(A, B, ents[e], bPath,
				    believed);
				var anywhere = false, t2;

				for (t2 = 0; t2 < targets.length; t2++) {
					if (accountedBy(A, B, ents[e],
					    targets[t2], believed).length > 0) {
						anywhere = true;
						break;
					}
				}
				/*
				 * A child no evidence reaches abstains, and
				 * abstentions do not block.  A child with
				 * evidence that does not reach B is an
				 * emigrant, and under v1 unanimity it does.
				 */
				if (!anywhere) continue;
				voters++;
				if (here.length === 0) {
					ok = false;
					break;
				}
				choices.push(here);
			}
			/*
			 * Vacuous fullness: an empty directory, or one
			 * whose children all died, holds toward every
			 * candidate and therefore asserts nothing.
			 */
			if (!ok || voters === 0) continue;
			if (!oneToOne(choices)) continue;
			winners.push(bPath);
		}
		/*
		 * Belief requires a unique target.  A unique CLAIMANT is
		 * deliberately not required, which is what allows fan-in
		 * (Remark 16.6): two directories may both be believed to
		 * have moved into one.
		 */
		if (winners.length === 1) believed[aPath] = winners[0];
		else if (winners.length > 1) believed[aPath] = null;
	}
	return (believed);
}

function correspondFace(aTree, bTree) {
	var A = indexTree(aTree), B = indexTree(bTree);
	var cands = closeCandidates(A, B);
	var believed = believeMoves(A, B, cands);
	var out = [], amb = [], k, n = 0;

	for (k in cands) if (hasOwn(cands, k)) n++;
	for (k in believed) {
		if (!hasOwn(believed, k)) continue;
		if (believed[k] === null) amb.push(k);
		else out.push({ from: k, to: believed[k] });
	}
	out.sort(function (x, y) { return (x.from < y.from ? -1 : 1); });
	amb.sort();
	return { candidates: n, moves: out, ambiguous: amb };
}

function correspond(spec) {
	return {
		onto: correspondFace(spec.trees.base, spec.trees.onto),
		offof: correspondFace(spec.trees.base, spec.trees.offof)
	};
}

if (typeof module !== "undefined") {
	module.exports = { correspond: correspond,
	    correspondFace: correspondFace, indexTree: indexTree };
}
