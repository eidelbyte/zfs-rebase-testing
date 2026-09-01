/*
 * succession.js -- set-valued succession and two-pass realization,
 * independently.
 *
 * WHAT THIS IS, AND WHERE ITS BOUNDARY FALLS.  Open problem 6 is
 * settled: succession is set-valued per pool -- a lost set and a
 * gained set, no antecedent labels, no degrade branch -- and
 * realization runs two passes, name-driven answers claiming first and
 * the lost-set fallback second, with both guards on both paths.
 *
 * This implements the DECISION PROCEDURE: given a pool's delta and
 * onto's fragmentation, where does the pool realize, and when does it
 * conflict.  It does NOT derive the deltas from the three trees, and
 * the distinction is the honest part of this file.
 *
 * correspond.js could be written standalone because directory
 * correspondence is computable from the three trees alone.
 * Succession is not.  Attributing a lost name to the right FRAGMENT
 * of a split pool needs the component -- in the claimed-guard shape,
 * base pool 10 holds {x, y}, off-of leaves y on 10 and splits x onto
 * a fresh dnode renamed aa, and saying that the delta lost {x}
 * belongs to the aa fragment rather than to the y fragment is a
 * pairing judgement, not a tree comparison.  Reimplementing pairing
 * here would not be a second opinion; it would be a worse copy of the
 * engine wearing one.
 *
 * So the deltas are an INPUT.  For the op6 fixtures they come from
 * each fixture's hand-worked WORKED OUTCOME block, which is exactly
 * the artifact wave 2 is gated on, and what is tested here is the
 * rule that consumes them.  When the engine can hand over real
 * deltas, this same rule takes them unchanged.
 *
 * The realization rules, from op6-set-succession.md:
 *
 *   PASS ONE, name-driven, per decided name: holder-star on onto,
 *     type filter, claimed skip.  Resolve and claim.
 *   PASS TWO, lost-set fallback, for whatever is still unresolved:
 *     the same two guards.  If the surviving lost names lie in one
 *     onto fragment, that fragment's dnode is the answer.  If they
 *     straddle, it conflicts.  If nothing survives the guards, the
 *     pool materializes.
 *
 * Two passes rather than one guarded pass, because a pool holding a
 * SURVIVING name has a better claim than one routing through a LOST
 * set and must win wherever the two fall in the walk order.  The
 * engine walks output pools in ascending first-name order, so a
 * single pass decides by sort accident -- see the claimed-guard pair.
 *
 * THAT WAS MEASURED, not assumed.  Substituting a one-pass loop with
 * the same two guards and running the op6 fixtures:
 *
 *   op6-claimed-guard         WRONG ANSWER.  /aa sorts first, takes
 *                             the dnode by the lost-set route, and
 *                             /y -- the pool holding the surviving
 *                             name -- materializes instead.
 *   op6-claimed-guard-mirror  RIGHT ANSWER, by luck.  /a sorts
 *                             first, so precedence and sort order
 *                             happen to agree.
 *
 * The mirror's value is that it PASSES.  Alone, claimed-guard's
 * failure could be any bug; the pair localizes it to sort order.
 * Note the levels differ: this runner also catches the mirror by
 * handing the pools over in both orders, but an engine run has no
 * such knob, so at the gold level only claimed-guard discriminates.
 */
"use strict";

var MATERIALIZE = "materialize";
var STRADDLE = "straddle";

function hasOwn(o, k) {
	return Object.prototype.hasOwnProperty.call(o, k);
}

/*
 * onto's view: which pool holds each name, and what type it is.
 * Built from a parsed tree so the caller passes spec.trees.onto
 * straight in.
 */
function ontoView(tree) {
	var v = { holder: {}, type: {} }, i, k, p;

	for (i = 0; i < tree.pools.length; i++) {
		p = tree.pools[i];
		v.type[p.key] = p.type;
		for (k = 0; k < p.names.length; k++)
			v.holder[p.names[k]] = p.key;
	}
	return (v);
}

/*
 * One output pool's candidates by the name-driven route: the onto
 * pools its DECIDED names resolve to, after the type filter.
 *
 * The type filter is not decoration on either route.  A decided name
 * whose onto holder is a directory cannot supply a dnode to a file
 * pool; an applier handed one would try to open a directory as a
 * file.
 */
function nameDriven(onto, pool) {
	var out = [], i, k;

	for (i = 0; i < pool.names.length; i++) {
		k = onto.holder[pool.names[i]];
		if (k === undefined) continue;
		if (onto.type[k] !== pool.type) continue;
		if (out.indexOf(k) < 0) out.push(k);
	}
	return (out);
}

/*
 * The same question for the LOST set, and the reason this route is
 * the riskier of the two: a decided name is a name the output pool
 * actually holds, while a lost name is only a name it used to hold.
 * Whatever stands at that name in onto need have nothing to do with
 * this pool -- it may be a directory somebody moved there -- so the
 * guards do more work here, not less.
 */
function lostDriven(onto, pool) {
	var out = [], i, k;

	for (i = 0; i < pool.lost.length; i++) {
		k = onto.holder[pool.lost[i]];
		if (k === undefined) continue;
		if (onto.type[k] !== pool.type) continue;
		if (out.indexOf(k) < 0) out.push(k);
	}
	return (out);
}

/*
 * Realize a set of output pools.  Each pool is
 * { name, names[], lost[], type }, where names are its DECIDED names
 * and lost is its delta's lost set.  Returns a map from pool name to
 * an onto pool key, MATERIALIZE, or STRADDLE.
 *
 * The pools are handed over in whatever order the caller likes and
 * the answer does not depend on it.  That independence is the point
 * of the two passes and is what the claimed-guard PAIR exists to
 * assert -- one fixture cannot state it, because a single ordering is
 * consistent with any rule.
 */
function unclaimed(list, claimed) {
	var out = [], i;

	for (i = 0; i < list.length; i++)
		if (!hasOwn(claimed, list[i])) out.push(list[i]);
	return (out);
}

function realize(onto, pools) {
	var claimed = {}, out = {}, later = [], i, cands;

	/* Pass one: every name-driven answer, claiming as it goes. */
	for (i = 0; i < pools.length; i++) {
		cands = unclaimed(nameDriven(onto, pools[i]), claimed);
		/*
		 * A decided name whose holder is already claimed cannot
		 * be reused, so the pool falls through to the fallback
		 * and from there usually to materialize.  Two output
		 * pools cannot share one onto dnode; that is what
		 * materialize is for, and an applier cannot make one
		 * inode be two files.
		 */
		if (cands.length === 0) {
			later.push(pools[i]);
			continue;
		}
		out[pools[i].name] = cands[0];
		claimed[cands[0]] = true;
	}

	/* Pass two: the lost-set fallback, over what is left. */
	for (i = 0; i < later.length; i++) {
		cands = unclaimed(lostDriven(onto, later[i]), claimed);
		if (cands.length === 0) {
			out[later[i].name] = MATERIALIZE;
			continue;
		}
		if (cands.length > 1) {
			/*
			 * The lost set straddles onto's fragments.  In the
			 * settled design routeDelta has already conflicted
			 * by now, so this branch is defensive rather than
			 * the conflict path.
			 */
			out[later[i].name] = STRADDLE;
			continue;
		}
		out[later[i].name] = cands[0];
		claimed[cands[0]] = true;
	}
	return (out);
}

/*
 * Consumer 2, content routing in splits: does a delta route to one of
 * onto's fragments, or does its lost set straddle them?
 *
 * This is where the residual OP6 conflict actually fires, which is
 * worth separating from realization's STRADDLE above.  In the settled
 * design realization never meets a straddle, because this rule has
 * already conflicted by the time realization runs; the branch up
 * there is defensive, not the conflict path.
 *
 * The routing unit is THE DELTA, not the name, and that decides
 * strictly more than the antecedent machinery it replaces: a
 * multi-name delta whose lost names all lie in one fragment routes
 * wholesale, which the old degrade got wrong.
 */
function routeDelta(onto, pool) {
	var cands = lostDriven(onto, pool);

	if (pool.lost.length === 0) return ({ route: null, straddle: false });
	if (cands.length > 1) return ({ route: null, straddle: true });
	if (cands.length === 0) return ({ route: null, straddle: false });
	return ({ route: cands[0], straddle: false });
}

/*
 * The one-move rule, restated on deltas.  No antecedents are
 * consulted: it reads two lost sets and two gained sets.
 *
 * Both halves matter.  A rule that reported whenever a name left both
 * deltas would get the disagreeing case right and the agreeing case
 * wrong, refusing work that agrees with itself -- which is why the
 * two fixtures for this are a pair.
 */
function oneMove(ontoDelta, offofDelta) {
	var both = [], i;

	for (i = 0; i < ontoDelta.lost.length; i++)
		if (offofDelta.lost.indexOf(ontoDelta.lost[i]) >= 0)
			both.push(ontoDelta.lost[i]);
	if (both.length === 0) return ({ conflict: false, names: [] });

	var a = ontoDelta.gained.slice().sort().join(" ");
	var b = offofDelta.gained.slice().sort().join(" ");

	/* Agreeing gains are the identical merge, not a collision. */
	return ({ conflict: a !== b, names: both });
}

if (typeof module !== "undefined") {
	module.exports = { ontoView: ontoView, realize: realize,
	    routeDelta: routeDelta, oneMove: oneMove,
	    MATERIALIZE: MATERIALIZE, STRADDLE: STRADDLE };
}
