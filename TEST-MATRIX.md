# Rebase test matrices

The methodology: for each engine phase, plot the problem space first --
enumerate the input dimensions, cross them into cells, and give EVERY
cell one of three dispositions:

- **covered**: a test exists; the test's comment names the cell.
- **existing**: an older test already exercises the cell; mapped here.
- **planned**: the matrix was written before its tests (as required);
  the row names the intended test and any helper it needs. Every
  planned row flips to covered when the test lands -- a planned row
  that survives a test-writing pass has to be re-dispositioned.
- **deferred**: not testable yet (missing helper, missing engine
  phase, environment limits), with the reason and the unblocking work.

A cell with no row in these tables is a hole. Rebases are fiendish;
a bug must have nowhere to hide. When a new engine phase lands, its
matrix is added here BEFORE its tests are written, and each test file's
header points back to its matrix section.

Matrices so far: setup (S), walk (W), linkpool (LP/LV), hysteria (H),
standalone-diff (D), move-collapse (M), linkpool-anchor (A
classification/rescue, T membership targets), membership-merge (U
unification, R resolution), content-merge-emit (E content, F
warnings/actions/emit, P samepath), cross-domain seam (X), and the
apply copy primitives (CP, plotted ahead of its tests -- the rows
are "planned" until apply-additions makes the helpers observable).
All six cross-reference phases are plotted.

THE CONTRACT FLIP (2026-08-23, with content-merge-emit, as
retrospective iceberg 5 mandated): the engine's success return is
now 0 with the summary manifest in outnvl; the ENOSYS sentinel is
retired, every direct assertion flipped in one pass, and
rt_run_rebase() hands the real nvlist over. The manifest is the
primary observable from the E/F/P sections on; the dbgmsg tally
lines remain byte-stable contracts for the earlier sections'
counters, now secondary signals exactly as the H and D preambles
promised. The crossref-era test tails (basic 2, hysteria 7, moves
10, linkpool 3, crossref 11) came alive in the same pass and are
mapped below as the P family's existing coverage -- only the two
linkpool-tail hardlink tests needed rewriting, to LINKPOOL_CONTENT
(pool-level conflicts are the v2 fix for per-member noise).

## Setup matrix (S) -- discovery, preconditions, fence-posts

Dimensions: left form {head, snapshot, missing}; right form {head,
snapshot, snapshot-in-left-chain, same-as-left, unrelated, missing};
name-semantics properties {defaults, mismatched, agreeing-insensitive,
agreeing-normalizing}; fence state {clean, leftover @%rebase-snap}.
Illegal combinations collapse; the surviving cells:

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| S1  | head left, related head right, defaults, clean | ENOSYS | existing: every smoke test in `basic` |
| S2  | right given as snapshot (no right fence-post) | ENOSYS | covered: test_setup_right_as_snapshot |
| S3  | left is a snapshot | EINVAL | existing: test_error_left_is_snapshot |
| S4  | left == right | EINVAL | existing: test_error_same_dataset |
| S5  | right snapshot already in left chain (linear) | EINVAL | covered: test_setup_linear_history |
| S6  | unrelated datasets, no common ancestor | ENOENT | covered: test_setup_unrelated_datasets |
| S7  | leftover @%rebase-snap from a crashed run | EEXIST | covered: test_setup_leftover_fence_snap |
| S8  | casesensitivity differs between sides | ENOTSUP | covered: test_setup_props_mismatch |
| S9  | all three agree on case-insensitive | ENOTSUP | covered: test_setup_case_insensitive_rejected |
| S10 | all three agree on a normalization | ENOTSUP | covered: test_setup_normalization_rejected |
| S11 | nonexistent left dataset | ENOENT | covered: test_setup_missing_left |
| S12 | active scrub/resilver | EBUSY | covered: test_setup_scrub_busy (spa_scan + POOL_SCRUB_PAUSE with a start/pause retry; pausing pins the tiny pool's scrub deterministically). First execution found an engine semantics bug: the check used dsl_scan_active(), which EXCLUDES paused scrubs and INCLUDES background cleanup (async destroys, pending frees) a rebase does not conflict with -- fixed to dsl_scan_scrubbing() || dsl_scan_resilvering(), the scan state machine engaged and nothing else |
| S13 | encryption mismatch left/right | EACCES | deferred: harness datasets are unencrypted; needs encrypted scaffold |
| S14 | non-ZPL dataset (zvol) as a side, SHARED lineage | ENOTSUP | covered: test_setup_zvol_side (rt_create_zvol_dataset + rt_clone build a zvol head and its clone so discovery finds a base and the type check fires; an UNRELATED zvol dies earlier with ENOENT in discovery -- the reachable-path analysis that first made this cell precise) |
| S15 | ZPL version < 5 on a side | ENOTSUP | covered: test_setup_zpl_version_low (rt_set_zplprop on the VERSION master-node key) |
| S16 | FUID table mismatch | ENOTSUP | covered: test_setup_fuid_mismatch (rt_set_zplprop writes the ZFS_FUID_TABLES key on one clone) |

## Walk matrix (W) -- union iteration, recursion, faults

Dimension 1, per-name presence (left, base, right), 2^3 minus the
empty triple = 7 cells. Dimension 2, per-slot kind {file, dir} at one
name: 2 uniform + 6 mixed for present-everywhere, plus mixed both-add.
Dimension 3, tree shape. Dimension 4, faults and non-files.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| W1  | present (L,B,R), untouched | ENOSYS | covered: test_walk_presence_matrix "p_all" |
| W2  | (L,B,-) right removed | ENOSYS | covered: presence matrix "p_lb" |
| W3  | (-,B,R) left removed | ENOSYS | covered: presence matrix "p_br" |
| W4  | (L,-,R) both added same name | ENOSYS | covered: presence matrix "p_lr" |
| W5  | (L,-,-) left-only add | ENOSYS | covered: presence matrix "p_l" |
| W6  | (-,-,R) right-only add | ENOSYS | covered: presence matrix "p_r" |
| W7  | (-,B,-) both removed | ENOSYS | covered: presence matrix "p_b" |
| W8  | dir on all three, children changed each side (unchanged-parent recursion) | ENOSYS | covered: test_walk_kind_matrix "d_rec" |
| W9  | same-name file/dir mixes: dff, ffd, dfd, fdf, ddf, fdd + both-add dir-vs-file | ENOSYS | covered: kind matrix "k_*" |
| W10 | empty directory on all sides | ENOSYS | covered: kind matrix "d_empty" |
| W11 | left-only nested subtree (with a hardlink pair inside: LP discovery through phase-2 recursion) | ENOSYS | covered: test_walk_side_subtrees "ldir" |
| W12 | right-only nested subtree | ENOSYS | covered: side subtrees "rdir" |
| W13 | both-added same-name dirs, different children | ENOSYS | covered: side subtrees "bdir" |
| W14 | base dir deleted with contents (one side; both sides) | ENOSYS | covered: test_walk_deleted_dir_trees |
| W15 | deep nesting (6 levels), leaf edited | ENOSYS | covered: test_walk_deep_nesting |
| W16 | empty filesystems everywhere | ENOSYS | covered: test_walk_empty_filesystems |
| W17 | pathless nlink-0 orphan in all snapshots (delete-queue sim, catalog test 13's walk half) | ENOSYS | covered: test_walk_orphan_skipped |
| W18 | dangling dirent (entry to unallocated obj) | EIO | covered: test_walk_dangling_dirent (was ENOENT until the first real run, 2026-08-22: the walk swallowed visit-level ENOENT as end-of-cursor, and a dangling reference is corruption anyway -- EIO like every corruption the walk detects) |
| W19 | hardlinked symlink / device node in a linkpool | ENOSYS | covered: test_walk_hardlinked_specials (helpers landed with hysteria cells H26/H28) |
| W20 | name at ZFS_MAX name length; huge directory (fat ZAP) | ENOSYS | deferred: fat-ZAP fixture is slow; add when a perf pass runs on the FreeBSD box |

## Linkpool matrix (LP discovery/membership, LV completeness)

Dimensions: links per pool {2, 3+}; link placement {one dir, across
dirs, inside side-added dirs}; pools per tree {1, several}; membership
delta per side {none, add, remove, dissolve-to-zero}; nlink-vs-entries
{equal, over, under, under-radar}; corrupted branch {left, base,
right}.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| LP1 | two links, one dir, untouched | ENOSYS | covered: test_linkpool_discovery_matrix "pair2" |
| LP2 | three links across three dir levels | ENOSYS | covered: discovery matrix "trio" |
| LP2b | hardlink pair inside a side-added subtree | ENOSYS | covered: test_walk_side_subtrees "ldir/h1,h2" |
| LP3 | several pools, discovery interleaved | ENOSYS | covered: discovery matrix "quad" |
| LP4 | same leaf name in two dirs, one dnode | ENOSYS | covered: discovery matrix "nm" |
| LP5 | two links in one directory | ENOSYS | covered: discovery matrix "pair2" |
| LP6 | side adds a link (nlink 3 -> 4) | ENOSYS | covered: test_linkpool_membership_ops "m4" |
| LP7 | side removes a link (3 -> 2) | ENOSYS | covered: membership ops "m3" |
| LP8 | side dissolves a pool to zero links | ENOSYS | covered: membership ops "d1,d2" |
| LV1 | nlink > entries on left | EIO | covered: test_linkpool_nlink_over_left (also the positive proof discovery ran) |
| LV2 | nlink > entries in base | EIO | covered: test_linkpool_nlink_over_base |
| LV3 | nlink > entries on right | EIO | covered: test_linkpool_nlink_over_right |
| LV4 | nlink < entries (3 entries, nlink 2) | EIO | covered: test_linkpool_nlink_under |
| LV5 | nlink == 1 with 2 entries | not detected | BY DESIGN: discovery keys on nlink > 1, so this corruption is invisible to the walker. Detecting it costs O(all files) memory -- exactly the tradeoff the design rejected. Documented, no test possible. |

Note on LV cells: originally the engine ASSERTed on the mismatch in
debug builds, which aborted the whole suite on the FreeBSD box (its
libzpool is a debug build -- LV1 proved it, 2026-08-22). Corrupt
on-disk input is an error, never an assertion: the engine now returns
EIO with a dbgmsg on every build, and the LV cells run anywhere.

The four older manifest-inspecting hardlink tests in test_linkpool.c
(edit-both-sides dedup, delete-no-conflict, delete-vs-edit) assert
crossref-era behavior and will be re-plotted into the crossref matrix
when that engine phase lands; until then they fail cleanly via the
defensive manifest accessors.

## Hysteria matrix (H) -- is_hysterical tiers, hysterical-detect

Dimensions: pair relationship {same obj untouched, same obj touched,
same obj gen-flipped, new obj same path}; change kind {none, data,
each SA identity attr, timestamps only, ACL, xattr value, xattr
representation, size, type flip}; object kind {file, dir, symlink,
device, linkpool member}; data-tier reachability {empty, single
block, multi-block, hole-vs-zeros, checksum-provable, embedded};
side {left, right}.

Observation mechanism: until standalone-diff emits changelists, the
only externally visible classification is the walk-summary dbgmsg
line ("rebase: walk visited N paths, hysterical left X right Y,
linkpool-member paths Z"). Tests assert (visited, hyst_left,
hyst_right, linked) tuples via a planned rt_walk_stats() helper that
parses the LAST matching line after a run. (Resolved while building
the helper: libzpool records dbgmsgs unconditionally --
zfs_dbgmsg_enable defaults on, no zfs_flags needed -- and
zfs_dbgmsg_print(fd, tag) dumps the ring to any fd.) Every H
cell gets re-asserted against real change records when standalone-
diff lands and this matrix's Expect column is upgraded; the tuples
below are the strongest assertion available today. Fixtures edit the
LEFT side unless the row says otherwise; H35 pins side symmetry.
Error-expectation rows (EIO) return through the normal path and are
unaffected by the dbgmsg mechanism. Unlike the LV rows, no H row
ASSERTs in debug builds; all are safe on either libzpool.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| H1  | file untouched since fork, same obj both sides (fast path; also pins the visited count) | hysterical on both sides | covered: test_hysteria_untouched_fast_path |
| H2  | untouched file whose dnode-block NEIGHBOR was edited (fast-path false positive must fall through tiers, not misclassify) | hysterical | covered: test_hysteria_neighbor_churn (adjacent objs, only the neighbor edited; the assertion holds whichever tier answers) |
| H3  | timestamps-only update, data untouched (touch(1) sim; BP_EQUAL tier) | hysterical | covered: test_hysteria_touch_only (rt_touch) |
| H4  | mode flip only (chmod) -- guards fast-path soundness: dn_blkptr births never change on a bonus-only edit | NOT hysterical | covered: test_hysteria_sa_identity "mode" |
| H5  | uid, gid, flags, projid each flipped in turn (rest of the fixed identity table; rdev is H29, size is H13) | NOT hysterical, each | covered: test_hysteria_sa_identity loops the attrs |
| H6  | DACL_ACES present on one side only / differing bytes | NOT hysterical | covered: test_hysteria_acl (raw SA byte blob via rt_set_sa_blob; memcmp compare needs no valid ACL) |
| H7  | same obj, identical content, ZPL_GEN flipped (recycled-slot sim) | NOT hysterical | covered: test_hysteria_recycled_gen (rt_set_sa_u64 on ZPL_GEN) |
| H8  | rename-on-save: new obj, same path, same content and identity | hysterical | covered: test_hysteria_rename_on_save |
| H9  | new obj, same path, different content | NOT hysterical | covered: test_hysteria_recreate_differs |
| H10 | same obj, rewritten with identical bytes (fletcher pool, so checksum is unprovable: tier 3 byte compare decides) | hysterical | covered: test_hysteria_rewrite_identical |
| H11 | same obj, rewritten with different bytes, same length | NOT hysterical | covered: test_hysteria_rewrite_differs |
| H12 | empty file on both sides (size-0 short circuit) | hysterical | covered: test_hysteria_empty_files |
| H13 | append: size differs, prefix identical | NOT hysterical | covered: test_hysteria_append |
| H14 | multi-block file (3+ blocks), identical rewrite (slow path crosses chunk boundaries) | hysterical | covered: test_hysteria_multiblock |
| H15 | hole in base vs explicit zeros on side, same logical bytes (tier 2 falls through on hole-vs-data; tier 3 says equal) | hysterical | covered: test_hysteria_hole_vs_zeros (rt_write_range) |
| H16 | checksum-provable fast path: sha256 dataset, identical rewrite of shared-nothing blocks | hysterical | deferred: needs a dataset checksum-property helper in the scaffold; add when property plumbing lands |
| H17 | embedded BP pair (identical and differing payloads) | per content | deferred: harness pool has no compression/embedded_data plumbing; revisit with H16's property helper |
| H18 | directory untouched since fork | hysterical | covered: test_hysteria_dir_untouched |
| H19 | directory whose only change is entries added/removed (ZPL_SIZE-skip rule: the dir stays hysterical, the children carry the change) | dir hysterical; child visits counted separately | covered: test_hysteria_dir_entries (the harness bumps the dir's entry-count size manually, matching real ZPL behavior it does not otherwise maintain) |
| H20 | directory chmod | NOT hysterical | covered: test_hysteria_dir_chmod |
| H21 | SA-form xattrs, same logical set both sides (different pack order) | hysterical | covered: test_hysteria_xattr_sa_equal (rt_set_dxattr) |
| H22 | SA-form xattr value differs | NOT hysterical | covered: test_hysteria_xattr_value |
| H23 | xattr present on one side only | NOT hysterical | covered: test_hysteria_xattr_added |
| H24 | representation flip: SA form in base, dir form on side, same logical set | hysterical | covered: test_hysteria_xattr_repr_flip (rt_remove_sa_attr + rt_add_xattr_dir_entry) |
| H25 | dir-form xattrs equal on both sides | hysterical | covered: test_hysteria_xattr_dir_equal |
| H26 | symlink recreated with the same target | hysterical | covered: test_hysteria_symlink_same (rt_create_symlink, unblocks W19) |
| H27 | symlink target changed | NOT hysterical | covered: test_hysteria_symlink_differs |
| H28 | device node, same rdev both sides, untouched-vs-recreated | hysterical | covered: test_hysteria_device_same (rt_create_device, unblocks W19) |
| H29 | device node rdev changed | NOT hysterical | covered: test_hysteria_device_differs |
| H30 | file replaced by dir (and dir by file) at one path | NOT hysterical | covered: test_hysteria_type_flip |
| H31 | linkpool member: content hysterical while a link is removed on the side (axis independence; retrospective-2 bug 2 regression) | hysterical AND linkpool counters move independently | covered: test_hysteria_linkpool_axis (counter-level); the full two-axis record assert is re-plotted at standalone-diff |
| H32 | side-vs-side adjacency: both sides made the same novel edit | out of scope for is_hysterical | BY DESIGN: handled by the crossref convergence check (doc, "Convergence check runs first"); row exists so the gap is recorded, not hidden |
| H33 | ZPL_GEN missing from a compared object | EIO | covered: test_hysteria_gen_missing (sa_remove works in libzpool; rt_remove_sa_attr) |
| H34 | symlink target flips SA-resident vs data-resident, same target | classified EDIT | BY DESIGN (conservative): logical-vs-representational unpacking is implemented for xattrs only; a false EDIT is safe, a false hysterical is not. Documented in the hysterical-detect worklog |
| H35 | side symmetry: H11's fixture (same-length differing rewrite) built on the RIGHT side | only the right counter moves | covered: test_hysteria_right_side (H8's all-hysterical fixture could not distinguish the counters, so the cell pivoted to H11's) |
| H36 | corrupt (unparseable) ZPL_DXATTR blob on a compared object | EIO, never the unpacker's EINVAL -- corrupt on-disk input classed correctly at the ioctl boundary (found 2026-08-23 by U9's plotting; fixed into hysterical-detect by chain rewrite) | covered: test_hysteria_dxattr_corrupt |

## Standalone-diff matrix (D) -- two-axis change records

Dimensions: content transition per (base, side) pair {absent->present
= ADD, present->absent = DELETE, present+identical = NONE (hysteria),
present+different = EDIT}; linkpool transition {none, join = ADDED,
leave = REMOVED, same lineage = NONE, moved/recycled = MOVED};
their legal cross product (ADD implies no base linkpool, DELETE
implies no side linkpool: 12 legal op pairs plus no-record); object
kind {file, dir}; side {left, right}; record-field conventions.

Observation mechanism: the walk logs a second dbgmsg line,
"rebase: changelists left %u right %u". Tests assert the full
six-tuple (visited, hyst_left, hyst_right, linked, changes_left,
changes_right) via a planned rt_changelist_counts() companion to
rt_walk_stats(), with fixtures minimal enough that every expected
tuple is computable by hand. SEMANTIC CHANGE at move-collapse
(2026-08-23): the changelists line now reports POST-collapse
counts -- what the diff pipeline hands to cross-reference. Only D6
had a collapsible pair, so only D6's tuple changed; diff_finish()
additionally asserts the moves line is all zeros for every D
fixture (the no-spurious-moves half of cell M16). Counts prove which paths produced or
suppressed a record -- and with per-cell fixtures that distinguishes
one-record-vs-two questions like EDIT-vs-DELETE+ADD -- but they
cannot prove op VALUES, provenance numbers, or field conventions;
those rows are explicitly deferred to the phases that consume them
(move-collapse, linkpool-anchor, emit). Fixtures edit the LEFT side
unless the row says otherwise. Side effects count: removing or
adding a hardlink touches the mate paths' nlink, so their
(hysterical, linkpool-only) records are part of each cell's
expected tuple. Tests land in a new test_diff.c section between
hysteria and moves.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| D1  | untouched tree | 0 records each side (with H1's hysteria tuple unchanged) | covered: test_diff_untouched |
| D2  | plain in-place edit | left 1 (EDIT x NONE), right 0 | covered: test_diff_edit |
| D3  | rename-on-save, content unchanged | 0 records -- not 1 (leaked EDIT) and not 2 (DELETE+ADD split) | covered: test_diff_hysterical_zero |
| D4  | standalone file added | left 1 (ADD x NONE) | covered: test_diff_add |
| D5  | standalone file deleted | left 1 (DELETE x NONE) | covered: test_diff_delete |
| D6  | plain rename | left 1 -- the pair now collapses to one MOVE record (was 2 pre-collapse; re-dispositioned 2026-08-23 when move-collapse landed, as this row promised). The collapse itself is M1's cell; this row keeps the D-level claim: a rename is never a surviving DELETE+ADD split | covered: test_diff_rename_collapses (renamed from test_diff_rename_two_records) |
| D7  | rename-on-save with NEW content | left 1 -- EDIT at the path, never a DELETE+ADD split (path-scoped content ops) | covered: test_diff_recreate_one_record |
| D8  | dir chmod | left 1 (the dir's own EDIT record) | covered: test_diff_dir_chmod |
| D9  | dir with entries-only change | left 1 -- the child's ADD; the dir itself contributes no record (ZPL_SIZE-skip rule at record level) | covered: test_diff_dir_entries |
| D10 | sever: member path replaced by identical standalone copy | left 2 linkpool-only records (severed path NONE x REMOVED via new obj; surviving mate NONE x REMOVED via nlink drop) -- the record-level retrospective-2 bug 2 regression, upgrading H31's counter-level assert | covered: test_diff_sever_identical |
| D11 | join: second name hardlinked onto a base standalone file | left 2 (new path ADD x ADDED; old path linkpool-only NONE x ADDED) | covered: test_diff_join |
| D12 | unlink one of three links | left 1 (DELETE x REMOVED); the two survivors produce nothing (same-lineage NONE x NONE) | covered: test_diff_unlink_survivors |
| D13 | dissolve a pair to zero links | left 2 (both DELETE x REMOVED) | covered: test_diff_dissolve |
| D14 | relink a member path into another linkpool, different content | left 2 (moved path EDIT x MOVED; abandoned mate NONE x REMOVED); target pool's paths produce nothing | covered: test_diff_relink_differs |
| D15 | recycled shared dnode (gen flip on a linkpool's obj) | left 2 (each member path EDIT x MOVED: lineage broke, numerically equal from/to) | covered: test_diff_recycled_pool |
| D16 | relink into another linkpool with IDENTICAL content | left 2 (moved path linkpool-only NONE x MOVED; abandoned mate NONE x REMOVED) | covered: test_diff_relink_identical |
| D17 | side symmetry: D2's fixture on the RIGHT | left 0, right 1 | covered: test_diff_right_side |
| D18 | both sides act on disjoint paths | left 1, right 1, independently | covered: test_diff_both_sides |
| D19 | side-added subtree containing a hardlink pair | left 3 (dir ADD x NONE; two paths ADD x ADDED) | covered: test_diff_added_subtree |
| D20 | rc_obj convention: DELETE carries base's obj, ADD the side's | rename halves match by obj | existing: M1 (a collapse can only happen if the two records met by object number; the promised move-collapse witness) |
| D21 | rc_linkpool_from/to provenance values | correct pool objs recorded | deferred: linkpool-anchor's split-fragment rescue reads provenance back; asserted there and at emit |
| D22 | content/linkpool op VALUES as values (EDIT vs ADD identity, ADDED vs MOVED identity) | per-record ops | deferred: counts prove record presence per fixture, not op identity; emit's manifest assertions take these over |
| D23 | rc_dn_type per record | matches the record's object | deferred: emit exposes records; assert there |
| D24 | classification error propagation (EIO out of the diff paths) | EIO aborts the rebase | existing: test_hysteria_gen_missing (H33 now flows through rebase_content_diff) |
| D25 | no-record invariant: NONE x NONE allocates nothing | absence | covered: asserted inside test_diff_untouched (whole tree) and test_diff_unlink_survivors (linkpool survivors) rather than a separate test |

## Move-collapse matrix (M) -- rename pairs to MOVE records

Dimensions: pair formation {pure rename, rename+edit, rename+
attr-touch, cross-directory, directory (with subtree), symlink};
linkpool guard shape {(NONE,NONE), (REMOVED,ADDED) same pool,
(NONE,ADDED) mixed, (REMOVED,NONE) mixed}; gen gate {match,
mismatch, unreadable}; candidate multiplicity {one ADD one DELETE,
several DELETEs, several ADDs}; near-miss inputs that must NOT
collapse {swap, rename-with-replacement, ADD-only, DELETE-only};
side {left, right}; selection tiebreak {prefix, tie}.

Observation mechanism: a third walk-summary dbgmsg line, byte-
stable from move-collapse on: "rebase: moves left %llu right %llu,
move-edits left %llu right %llu", scraped by rt_move_stats(). M
tests assert the full ten-tuple (the D six-tuple plus the four
move counters) via moves_finish(); changelist counts are post-
collapse, so every collapse is visible twice (a move counted, a
record gone). Counters prove that and how many collapses happened
and their MOVE/MOVE_EDIT split; they cannot prove WHICH DELETE a
promotion consumed (the surviving records' paths and rc_old_path
are invisible until emit) -- those rows are deferred to emit, the
same rule that deferred D20-D23 to their consumers. Hysteria
counters keep their walk-time semantics: the collapse's content
gate reuses the tiers but never increments them. Fixtures edit the
LEFT side unless the row says otherwise, and hand-compute side
effects exactly like the D tuples (unchanged both-present paths
count as hysterical on their side).

Unrepresentable combinations, so their absence is explained and
not a hole: (1) mixed guard eligibility WITHIN one run -- every
DELETE in a run derives its linkpool op from the base pool state
of the run's one dnode and every ADD from the side pool state, so
all DELETEs agree and all ADDs agree; the guard is written
per-pair for robustness but degenerates to run-level. (2) MOVED
ops on pair candidates -- MOVED needs the path present on both
sides, ADD needs base absent, DELETE needs side absent. (3) A
linkpool-only record as a pair candidate -- content NONE is
neither ADD nor DELETE; such records ride in runs untouched (M6's
fixture has the mate path present as a non-record; severed-mate
linkpool-only records always carry a different rc_obj than the
severed path's new dnode).

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| M1  | pure standalone rename (fast-path content gate: helper rename leaves the dnode clean) | left: 1 MOVE, changelists 1/0 -- D6's flip and D20's rc_obj witness | covered: test_moves_pure_rename |
| M2  | rename + content edit | left: 1 MOVE_EDIT, moves 0, move-edits 1 | covered: test_moves_move_edit |
| M3  | rename + timestamp touch (dnode dirtied, content identical) | MOVE, not MOVE_EDIT: the content gate runs the full tiers past the failed fast path, and timestamps are excluded from identity | covered: test_moves_rename_touched |
| M4  | cross-directory move | 1 MOVE (path scoping holds across parents) | covered: test_moves_cross_dir |
| M5  | directory rename with one child | left: 2 MOVEs (the dir and the child collapse independently; per-descendant records are the contract, dedup belongs to the apply compiler) | covered: test_moves_dir_rename |
| M6  | member-path rename, (REMOVED(from=N), ADDED(to=N)) guard shape | 1 MOVE; the mate path yields no record; refcount neutral | covered: test_moves_member_rename |
| M7  | guard mixed (NONE, ADDED): rename + new hardlink onto the dnode in the rename window | NO collapse: 3 records survive (DELETE + 2 ADD), moves 0 | covered: test_moves_guard_added |
| M8  | guard mixed (REMOVED, NONE): member rename while the pool dissolves (mate unlinked) | NO collapse: 3 records survive, moves 0 -- the conservative documented case; the rename is deliberately not detected | covered: test_moves_guard_dissolved |
| M9  | gen mismatch (ZPL_GEN flipped on the side's dnode) | NO collapse: recycled lineage, 2 records survive, moves 0 -- never MOVE_EDIT | covered: test_moves_gen_mismatch |
| M10 | gen unreadable at collapse time (ZPL_GEN removed; a rename's records never read gen during classification, so collapse is the FIRST reader) | EIO aborts the rebase | covered: test_moves_gen_missing |
| M11 | several eligible DELETE candidates (pool of 3: one member renamed, another unlinked) | 1 MOVE + 1 surviving DELETE (changelists 2); collapse count proves one pair formed | covered: test_moves_two_delete_candidates |
| M12 | several ADDs drain several DELETEs (both members of a pair renamed) | 2 MOVEs, changelists 2/0 | covered: test_moves_both_members_renamed |
| M13 | side independence: both sides rename the same file (differently) | 1 MOVE each side, changelists 1/1, moves 1/1 -- the counter-level half of the future MOVE_DIVERGE fixture | covered: test_moves_both_sides |
| M14 | swap two files (three renames, net obj exchange at two paths) | 0 moves: two EDIT records (path-scoped content ops; no ADD/DELETE exists to pair). v1 limit, documented | covered: test_moves_swap |
| M15 | rename + new file created at the old path | 0 moves: EDIT at the old path (different dnode) + surviving ADD at the new (its run has no DELETE). v1 limit, documented | covered: test_moves_replaced_source |
| M16 | no spurious moves | ADD-only and DELETE-only runs never collapse; every D fixture's moves line is all zeros | existing: diff_finish() asserts zero moves across all 19 D tests (D4/D5 are the pure ADD-only/DELETE-only cells) |
| M17 | symlink rename | 1 MOVE (var-attr identity through the collapse content gate) | covered: test_moves_symlink_rename |
| M18 | prefix tiebreak selection VALUE (in-dir source wins over a distant candidate; equal prefixes keep sort order) | which DELETE was consumed / rc_old_path | deferred: invisible in counts (M11 proves the collapse, not the choice); emit's manifest exposes rc_old_path and takes these over |

## Linkpool-anchor matrix (A classification/rescue, T targets)

Dimensions, phase A: classification input {absent from base =
NOVEL, present+untouched = ANCHORED (fast path), present+touched+
gen-match = ANCHORED, present+touched+gen-mismatch = RECYCLED};
base state of the dnode {pooled, standalone (the degenerate
one-member anchor)}; fragment rescue {clean fragment, fragment
plus ADDED members, two distinct parents (no rescue), parent ==
own obj i.e. recycled-in-place (no rescue), no MOVED provenance at
all}; pools per side; side symmetry; gen unreadable at classify.
Dimensions, phase B: each target kind {SAME_AS_BASE, GONE,
STANDALONE, ANCHOR, FRAGMENT, NOVEL} crossed with how it is
reached (record-driven, table-driven, synthesized), plus dual-sided
rows and the row-count consistency invariant.

Observation mechanism: four new byte-stable dbgmsg lines --
"rebase: linkpools left|right anchored %llu novel %llu recycled
%llu fragment %llu" (rt_anchor_stats()) and "rebase: targets
left|right same %llu gone %llu standalone %llu anchor %llu
fragment %llu novel %llu" (rt_target_stats()). A/T tests assert a
27-value expectation via ab_finish(): linkpool-member paths (the
one walk counter that validates a pool fixture), both changelist
counts, all four move counters, the eight anchor tallies, and the
twelve target tallies. The visited and hysteria counters are
deliberately NOT asserted here: they are pure walk-classification
observables already pinned exhaustively by the H, D, and M
sections on fixtures built from the same helpers, and they are the
most error-prone numbers to hand-compute on pool-heavy fixtures.
Every row carries one target per side (SAME_AS_BASE = expressed
nothing), so each side's target tallies sum to the row count;
ab_finish() checks the two sums against each other structurally
on every test (cell T8). Fixtures edit the LEFT side unless the
row says otherwise.

Not fixturable without on-disk corruption injection: a hard
dmu_object_info() error during classification (ENOENT is the
legitimate NOVEL answer; anything else needs a damaged base). The
EIO conversion boundary it would cross is A9's.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| A1  | untouched base pool | ANCHORED on both sides via the fast path; members SAME/SAME | covered: test_anchor_untouched_pool |
| A2  | pool content edited in place | left ANCHORED via the gen compare (fast path fails); both member paths carry EDIT records, targets still SAME (same lineage in the base table) | covered: test_anchor_edited_pool |
| A3  | post-fork pool (create + link) | NOVEL, anchor 0; members target NOVEL(pool obj) | covered: test_anchor_novel_pool |
| A4  | recycled index (gen flipped on the pool dnode) -- THE MANDATORY CELL | RECYCLED, never ANCHORED; members EDITxMOVED(from==to) target NOVEL; the rescue's parent==own-obj guard holds (no false FRAGMENT) | covered: test_anchor_recycled_pool |
| A5  | pool grown from a base-standalone dnode | ANCHORED (lineage, not table); both members target ANCHOR -- base contributes an absent opinion | covered: test_anchor_degenerate_standalone |
| A6  | clean split fragment (two members severed to one new dnode, identical content) | fragment pool NOVEL rewritten to SPLIT_FRAGMENT(parent); members target FRAGMENT; abandoned mate targets STANDALONE | covered: test_anchor_fragment |
| A7  | fragment plus a post-split ADDED member | still SPLIT_FRAGMENT (ADDED is neutral); all three members target FRAGMENT | covered: test_anchor_fragment_added_member |
| A8  | one new pool drawing members from TWO base pools | two distinct parents disqualify: stays NOVEL, members target NOVEL | covered: test_anchor_two_parents |
| A9  | ZPL_GEN unreadable at classify time | EIO -- and the fixture repoints the old path onto a fresh dnode so no walk hysteria/linkpool compare and no move-collapse run reads the gen first: phase A is genuinely the first reader | covered: test_anchor_gen_missing |
| A10 | link churn: every base path of the dnode turned over, nlink never zero | ANCHORED -- the doc-correction witness (the survivors scan would falsely RECYCLE); new paths target ANCHOR, old path GONE | covered: test_anchor_link_churn |
| A11 | several pools on one side (anchored + novel) | tallies count independently: left (1,1,0,0) | covered: test_anchor_multi_pool |
| A12 | side symmetry: novel pool on the RIGHT | right tallies and right NOVEL targets; left all SAME | covered: test_anchor_right_novel |
| T1  | GONE via a pooled member's DELETE record | deleted path GONE, survivors SAME (pool still anchored) | covered: test_target_gone_delete |
| T2  | GONE synthesized for a collapsed member-move's old path | MOVE record at the new path (ANCHOR target, ml=1), old path row GONE without any record, mate SAME | covered: test_target_gone_move_source |
| T3  | STANDALONE via sever (hysterical copy) | severed path and abandoned mate both STANDALONE; left side has NO pools at all (tallies all zero) | covered: test_target_severed_standalone |
| T4  | SAME_AS_BASE as the unexpressed side | every cell above asserts full right-side (or left-side) SAME columns | existing: all A/T fixtures |
| T5  | ANCHOR via relink into another anchored pool | moved path targets ANCHOR(new pool), abandoned mate STANDALONE, target pool's own members SAME | covered: test_target_relink_anchor |
| T6  | NOVEL targets | members of NOVEL and RECYCLED pools | existing: A3, A4, A8, A12 |
| T7  | FRAGMENT targets | fragment members, including post-split joins | existing: A6, A7 |
| T8  | row-count consistency: each side's target tallies sum to the row count | structural | existing: ab_finish() checks left sum == right sum on every test |
| T9  | dual-sided row: left deletes the path, right severs it | one row, GONE vs STANDALONE; the dissolved pool's mate STANDALONE on both sides; zero pools remain anywhere | covered: test_target_both_sides |

## Membership-merge matrix (U unification, R resolution)

Dimensions, phase C (U): pool kind pairing {fragment-fragment same
parent, fragment-fragment cross-parent, novel-novel, recycled as
novel}; roster relation {partial overlap, zero overlap}; data
relation for novels {identical, different}; chain shape {simple
pair, three pools bridged through one}; data-compare fault. 
Dimensions, phase D (R): the merge table's rows {agreement, lone
expression wins, GONE vs STANDALONE (policy), GONE vs linkpool
destination, contradictory destinations incl. fragment-vs-anchor},
the dead-pool sweep {dead+edited, dead+silent, conflicted member
keeps alive, joins keep alive, sever-vs-edit orthogonality}, and
manifest dedup {same-type alt-path merge}.

Observation mechanism: two new byte-stable lines -- "rebase:
finals same %llu gone %llu standalone %llu anchor %llu fragment
%llu novel %llu conflict %llu" (rt_final_stats(); the conflict
bucket counts rows left undecided) and "rebase: conflicts total
%llu relink %llu divergent %llu overlap %llu content %llu"
(rt_conflict_stats(); post-dedup record counts). U/R tests assert
a 12-value expectation via mm_finish(), which also scrapes the
targets lines to check sum(finals buckets) == row count
structurally on every test. Earlier-phase counters are owned by
their own sections (the A/T rationale).

What the tallies can and cannot see, recorded so the deferrals
are honest: UNIFICATION IS VISIBLE THROUGH THE CONFLICT BUCKET --
an un-unified shared row is either skipped (both-NOVEL) or
divergent (fragments), so zero conflicts on an overlapping fixture
proves the ids merged. Zero-overlap DISTINCTNESS is invisible
(separate groups tally identically to one), so U2/U6 pin the
no-conflict outcome and defer id distinctness to emit's group
exposure. UPDATE 2026-08-24: that invisibility was hiding a real
bug -- X16's first execution found lockstep raw ids gluing two
single-side pools into ONE group (a false LINKPOOL_CONTENT in the
final manifest for U5's very shape). The mechanism is now fixed
(rebase_relabel_unpaired gives every unpaired pool its own
synthetic id), and X16 witnesses the conflict-count side of it;
ASSERTING distinctness positively still waits for emit-part-2's
group exposure, so U2/U6's deferral stands for the assertion
only. Policy arms other than NONE are unreachable until the
ioctl plumbs flags (R14).

The U9 fault fixture uses a malformed ZPL_DXATTR blob, not a
missing ZPL_SIZE: a missing identity attribute reads as
"different" in the SA tier (present-vs-absent is an answer, not
an error), which would resolve as an overlap conflict instead of
failing -- only the xattr unpack stage turns corruption into EIO
before the size read. (Plotting this cell found the unpack error
leaking as EINVAL -- fixed into hysterical-detect via chain
rewrite; H36 pins the walk-path variant.)

Unrepresentable, so its absence is explained: OVERLAPPING
FRAGMENTS OF DIFFERENT PARENTS. A fragment claiming another
parent's base path always sees that path's MOVED provenance from
the other parent, so the rescue either agrees on one parent or
declines to NOVEL on two -- cross-parent FRAGMENT-vs-FRAGMENT can
never meet in a row. U3 covers the realizable neighbor: FRAGMENT
against the NOVEL pool that a two-parent decline produces.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| U1  | fragments of one parent, partial overlap (the doc's {B,C}/{C,D} worked example) | unified: roster {B,C,D} all FRAGMENT finals, zero conflicts (un-unified would put the shared row in divergent) | covered: test_merge_fragment_overlap |
| U2  | fragments of one parent, ZERO overlap | stay separate (rule 5): all members FRAGMENT finals, zero conflicts; id distinctness deferred to emit | covered: test_merge_fragment_disjoint |
| U3  | fragment meets the two-parent decline: left fragments {c,d} out of P1, right builds a pool from P1's c plus P2's y (declined to NOVEL) | the shared row is FRAGMENT vs NOVEL = DIVERGENT_MEMBERSHIP; the decline itself is A8's cell | covered: test_merge_fragment_vs_novel |
| U4  | novel pools, overlap, identical data | unified: all NOVEL finals, zero conflicts (un-unified would leave shared rows undecided) | covered: test_merge_novel_unified |
| U5  | novel pools, overlap, different data | NOVEL_LINKPOOL_OVERLAP scoped to exactly the overlapping paths; those rows stay undecided, the rest resolve NOVEL. First execution (2026-08-23) caught a real engine bug: clones allocate object numbers in lockstep, so the two raw novel ids collided numerically and merge_row's equality test resolved the conflicted row -- fixed with the synthetic-id guard (REBASE_ID_IS_UNIFIED); both-NOVEL equality now means agreement only for unified ids. Postscript 2026-08-24: the same lockstep collision was ALSO gluing this fixture's two pools into one final GROUP (a false LINKPOOL_CONTENT in the manifest this test never reads) -- caught by X16, fixed by rebase_relabel_unpaired | covered: test_merge_novel_overlap_conflict |
| U6  | novel pools, zero overlap, identical data | stay separate (rule 5): NOVEL finals, zero conflicts; distinctness deferred to emit | covered: test_merge_novel_disjoint |
| U7  | chained unification: two left pools bridged by one right pool | one group, zero conflicts (a broken chain leaves the bridge rows undecided) | covered: test_merge_novel_chain |
| U8  | both sides recycled the same pool identically | RECYCLED pools enter the heuristic and unify: NOVEL finals, zero conflicts | covered: test_merge_recycled_unified |
| U9  | data compare fault (malformed DXATTR on the right pool dnode; phase C is its first reader) | EIO | covered: test_merge_data_fault |
| R1  | THE ACCEPTANCE CASE: base {A,B,C}, left severs A, right idle | A STANDALONE, survivors SAME, zero conflicts, no resurrection | covered: test_merge_sever_acceptance |
| R2  | agreement: both sides sever the same path | STANDALONE, zero conflicts | covered: test_merge_sever_agreement |
| R3  | lone GONE wins on a pooled member | final GONE, zero conflicts | covered: test_merge_lone_gone |
| R4  | GONE vs STANDALONE under POLICY_NONE | DELETE_VS_RELINK, row undecided | covered: test_merge_gone_vs_standalone |
| R5  | GONE vs ANCHOR (delete vs join) | DELETE_VS_RELINK | covered: test_merge_gone_vs_anchor |
| R6  | divergent joins: left relinks into P1, right into P2 | DIVERGENT_MEMBERSHIP, row undecided | covered: test_merge_divergent_joins |
| R7  | FRAGMENT vs ANCHOR(other pool) on one path | DIVERGENT_MEMBERSHIP; the conflicted row keeps the parent alive (no content conflict compounding) | covered: test_merge_fragment_vs_anchor |
| R8  | dead pool, other side edited: left unlinks all members, right edits content | ONE LINKPOOL_CONTENT record with the member list as alt paths; per-path rows resolve GONE cleanly | covered: test_merge_dead_pool_edited |
| R9  | dead pool, other side silent | zero conflicts (control for R8) | covered: test_merge_dead_pool_silent |
| R10 | conflicted member keeps the pool alive | left unlinks all, right severs one + edits: only the DELETE_VS_RELINK fires, never LINKPOOL_CONTENT | covered: test_merge_conflicted_keeps_alive |
| R11 | joins keep the pool alive (link churn at finals level) | old name GONE, new names ANCHOR, right edit does NOT fire LINKPOOL_CONTENT | covered: test_merge_churn_alive |
| R12 | sever vs content edit are orthogonal intents | left severs A, right edits pool: A STANDALONE, zero conflicts (the SHRUNK warning is phase F's, not a conflict) | covered: test_merge_sever_vs_edit |
| R13 | GONE vs FRAGMENT | DELETE_VS_RELINK keyed by the fragment id | covered: test_merge_gone_vs_fragment |
| R14 | policy arms LEFT/RIGHT/BASE/NEITHER for the R4 tie | silent resolution per flag | deferred: rs_policy is always NONE until libzfs-rebase/cli-rebase plumb flags; the arms are implemented and this cell unblocks then |
| R15 | standalone-only run resolves trivially | finals land in same/gone buckets, zero conflicts, sum invariant holds with no pools anywhere | covered: test_merge_standalone_trivial |
| R16 | manifest dedup: two same-type conflicts on one destination | left deletes two members, right relinks both into one pool: ONE DELETE_VS_RELINK record, second path in alt_paths | covered: test_merge_dedup_alt_paths |

## Content-merge-emit matrix (E content, F warnings/actions/emit, P samepath)

Dimensions, phase E: group edit shape {neither, one side, both
convergent, both divergent}; group kind {anchored, fragment
(one-sided split three-way), novel}; dead-pool interplay (owned by
the R family). Phase F: warning kinds {IMPLIED_CHANGE,
LINKPOOL_SHRUNK, DANGLING_SYMLINK (caused / pre-existing /
relative / absolute)}; action compilation {right-driven vs
left-expressed}; emit shape {counts, typed conflicts, alt arrays,
truncated flag}. Samepath (P): the carried sprint-1 logic --
every pair shape is pinned by the revived crossref-era sections.

Observation mechanism: the manifest itself, via rt_run_rebase()
and the accessors (nconflicts/nwarnings/nactions, typed conflict
and warning lookups, alt counts, changelist counts).

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| E1  | PHANTOM-CONFLICT DISSOLUTION (the second acceptance test): base pool {A,B,C,X,Y}; left edits via its {A,B,C} view and severs X,Y; right edits via {C,X,Y} to the SAME content and severs A,B | one lineage, convergent edit, disjoint severs: ZERO conflicts | covered: test_emit_phantom_dissolution |
| E2  | both sides edit a pool to the same value | convergent, zero conflicts | covered: test_emit_convergent_pool (E1 also witnesses at scale) |
| E3  | right-only pool edit | zero conflicts; WRITE action compiled; IMPLIED_CHANGE per left-silent member | covered: test_emit_right_pool_edit |
| E4  | divergent pool edits | one LINKPOOL_CONTENT with the mate as alt path | existing: test_edge_hardlink_edit_both (rewritten to v2 in the flip pass) |
| E5  | dead pool with a lost edit | one LINKPOOL_CONTENT (dead-pool sweep) | existing: test_edge_hardlink_delete_vs_edit (rewritten); R8 pins the counter view |
| E6  | one-sided split merges three-way: left severs a fragment, right edits the parent | fragment inherits right's edit (l == base, r edited): zero conflicts, WRITE actions | covered: test_emit_fragment_three_way |
| E7  | unified novel group content | both sides identical by construction | existing: U4/U7/U8 |
| F1  | IMPLIED_CHANGE per unlooked-at member | right pool edit: one warning per left-silent member path | covered: test_emit_right_pool_edit (asserts both) |
| F2  | LINKPOOL_SHRUNK when the winner's roster lost members | left severs A, right edits: A standalone, edit wins, SHRUNK warned | covered: test_emit_shrunk_warning |
| F3  | right-driven standalone changes compile actions | add+edit+delete on right = COPY+WRITE+UNLINK (nactions 3). First execution (2026-08-23) caught a real double-count: the row loop and the standalone sweep both emitted UNLINK for the same right delete; fixed with the ownership gate (row loop skips standalone paths) | covered: test_emit_standalone_actions |
| F4  | left-expressed changes compile NO actions | left-only changes: nactions 0 | covered: test_emit_left_no_actions |
| F5  | merge-caused dangling symlink | right adds a link to a name left renamed away: DANGLING_SYMLINK warned | covered: test_emit_dangling_symlink |
| F6  | pre-existing dangle suppressed | base symlink already dangling, nobody touches it: zero warnings | covered: test_emit_dangling_preexisting |
| F7  | relative target resolution | subdir symlink "../hello", left deletes hello: warned | covered: test_emit_dangling_relative |
| F8  | surviving target, no warning | symlink whose target survives the merge stays quiet | covered: inside test_emit_dangling_symlink (control link) |
| F9  | conflicted row never stacks a dangling warning | undecided paths count present | deferred: needs a conflicted-target fixture whose symlink is otherwise clean; add with phase-2 resolve tests |
| F10 | truncated flag past REBASE_EMIT_MAX_CONFLICTS | totals full, first 512 records, truncated true | deferred: a 512-conflict fixture is a perf-pass item; the flag's false value is asserted by every manifest test implicitly |
| P1  | BOTH_MODIFIED (same obj, and cross-obj replace-vs-edit) | | existing: test_conflict_both_modified, basic deep-nested, hysteria-tail different-content pair |
| P2  | CREATE_CREATE incl. file-vs-dir | | existing: test_conflict_create_create, test_edge_file_vs_dir |
| P3  | MODIFY_DELETE / DELETE_MODIFY | | existing: test_conflict_modify_delete, test_conflict_delete_modify |
| P4  | DIR_DELETE_VS_EDIT, both directions | | existing: test_conflict_dir_delete_vs_edit + reverse |
| P5  | convergence suppression (identical edits/adds/move-edits, hysterical no-ops) | zero conflicts | existing: the five suppression tests in hysteria's tail |
| P6  | MOVE family: diverge (both dests), move-vs-edit (dest and obj), move-vs-delete both directions, benign same-dest pairs | | existing: the ten manifest tests in moves' tail |
| P7  | both-DELETE and clean-merge shapes | zero conflicts | existing: test_benign_both_delete, the two clean tests |
| P8  | multiple conflicts, one manifest; counts | 3 records; changelist counts serialize | existing: test_edge_multiple_conflicts, test_edge_changelist_counts |

## Cross-domain seam matrix (X) -- the membership/content boundary

Plotted 2026-08-24, BEFORE the fix commit, per the retrospective-4
lesson: when two referees split a space, the seam itself is a cell
family. Planning doc: sprints/sprint-2/membership-content-interface.md.
The leak under test: a row where one side holds a pool destination,
the other side deferred with SAME_AS_BASE while holding a
content-bearing record (ADD, EDIT, MOVE, MOVE_EDIT) at the path,
and that record's dnode is foreign to the destination's lineage.
The fix is one consultation in merge_row plus widening the
synthesized GONE row to every collapsed move's old path.

Dimensions: base state at the path {new name, standalone dnode M,
base pool member (control ground)}; the deferring side's claim
{none, ADD, in-place EDIT, replace-EDIT, MOVE, MOVE_EDIT}; winning
destination kind {ANCHOR grown around the claim's own dnode (GROW),
ANCHOR around a foreign file (REPOINT), NOVEL, FRAGMENT}; data
relation {different, identical}; polarity {left defers, right
defers}.

Two rules discovered while walking the code, pinned here so the
tests hold them: (1) THE BASE-LINEAGE EXEMPTION -- a SAME_AS_BASE
deferral can also come from the "anchored, matching base" arm on
base-pool ground, where the claim is a pool-level edit of the base
lineage that the group merge and dead-pool scan already own; the
consultation must let a claim whose object IS the path's base
lineage defer, or the section-5 orthogonality table (edit versus
relink, SHRUNK warning, no conflict) regresses. X17 exists to
catch exactly that. (2) NEVER COMPARE NOVEL IDS -- the U5 lockstep
lesson applied at the seam: a raw cross-side object match is a
coincidence and a synthetic unified id is not a readable object,
so the on-lineage test uses only base-rooted identities (ANCHOR
lineage, FRAGMENT parent) and the data-compare object comes from
the expressing side's own linkpool table at the path.

Observation mechanism: the manifest (typed conflicts, warnings,
action counts) plus the mm_finish tuple family -- a consultation
conflict leaves its row undecided, so it lands in the finals
conflict bucket and the sum invariant still holds.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| X1  | X-A: left creates plain P (new name); right creates P as a member of a novel two-name pool, different content | CREATE_CREATE keyed by the destination, row undecided (finals conflict 1); the undecided row compiles nothing at P, but the uncontested remainder proceeds: the right-won group's WRITE plus the other member's LINK (nactions 2 -- two box runs calibrated this pin from 0 and 1: row/group actions are guarded by row state and group source, conflict coverage guards the record-driven sweep) | covered: test_seam_create_vs_pool |
| X2  | X-A mirror: right creates plain P; left pools it, different content | CREATE_CREATE, symmetric | covered: test_seam_create_vs_pool_mirror |
| X3  | X-A convergent: left creates P with bytes identical to the file right linked P to (ANCHOR winner) | zero conflicts; P joins the pool (final ANCHOR); convergence defers | covered: test_seam_create_convergent |
| X4  | X-B edit flavor: base standalone M at P; left edits M in place; right repoints P into a pool around foreign base file M2 | BOTH_MODIFIED, row undecided; the edit is no longer silently discarded | covered: test_seam_edit_vs_repoint |
| X5  | X-B replace flavor: left replaces P with a new dnode; right GROWS a pool around M keeping P | BOTH_MODIFIED: growing is only safe for on-lineage claims, and a replace swapped the dnode out from under the grow | covered: test_seam_replace_vs_grow |
| X6  | X-B mirror: right edits M; left repoints P | BOTH_MODIFIED, symmetric | covered: test_seam_edit_vs_repoint_mirror |
| X7  | X-B convergent: left edits M to exactly the destination pool's bytes | zero conflicts; P final ANCHOR(M2); the expressed sharing stands, the content is satisfied | covered: test_seam_edit_convergent |
| X8  | X-C grow flavor: left moves P away (standalone M); right grows a pool around M keeping name P | DELETE_VS_RELINK at old path P via the widened GONE row | covered: test_seam_move_vs_grow |
| X9  | X-C repoint flavor, member-pool destination: left moves P away; right relinks P into base pool N | DELETE_VS_RELINK keyed N | covered: test_seam_move_vs_repoint |
| X10 | X-C mirror: right moves P away; left pools the name | DELETE_VS_RELINK, symmetric | covered: test_seam_move_vs_grow_mirror |
| X11 | X-D: left moves M to P2; right independently creates P2 as a pool member, different content | MOVE_VS_EDIT keyed by the destination (the MOVE record is a content claim at its new path), row undecided | covered: test_seam_move_dest_vs_pool |
| X12 | X-D mirror: right moves onto a path left pooled | MOVE_VS_EDIT, symmetric | covered: test_seam_move_dest_vs_pool_mirror |
| X13 | X-D convergent: the destination pool's bytes equal the moved file's | zero conflicts; P2 joins the pool | covered: test_seam_move_dest_convergent |
| X14 | X-E: left MOVE_EDITs M from P to P2; right grows a pool {P,P3} around M | exactly ONE conflict, DELETE_VS_RELINK at old path P; no MOVE_VS_EDIT or BOTH_MODIFIED noise at P2 (the lineage is shared; the old-path contest owns the tension) | covered: test_seam_pool_around_moved |
| X15 | X-CTL-GROW control: left edits M in place; right grows a pool around M | zero conflicts; P final SAME/ANCHOR ground; the on-lineage claim defers and the group owns the content -- pins that GROW keeps working | covered: test_seam_ctl_grow |
| X16 | X-CTL-DUAL control: left links P into a left novel pool, right links P into a right novel pool, different data | NOVEL_LINKPOOL_OVERLAP at P, row undecided: dual records contest properly, never defer (the dual-record lemma). First execution (2026-08-24) caught a real engine bug one level above U5's: un-unified pools kept RAW ids in their targets, lockstep allocation collided them across sides, and rebase_group glued the two single-side pools into one group -- a false LINKPOOL_CONTENT in the final manifest (U5's own fixture carried the same phantom group, unseen: mm_finish reads the membership-time dbgmsg, never the final manifest). Fixed by rebase_relabel_unpaired: every unpaired branch pool identity gets its own synthetic id, so a shared id can only mean phase C proved correspondence | covered: test_seam_ctl_dual |
| X17 | X-CTL-BASEPOOL control: base pool N {A,A2,P} (three names, so the right-side remnant is still a pool and the dead-pool rules stay out of the picture); left edits N's content; right repoints P into a pool around M2 | ZERO consultation conflicts (base-lineage exemption): P final ANCHOR(M2), left's edit lands through N's surviving group; catches a consultation missing the exemption. E6 pins the FRAGMENT-winner variant of the same exemption | covered: test_seam_ctl_basepool |
| X18 | X-REG regression: plain left standalone rename, nobody contests | diff tuples unchanged from M1; the widened synthesis adds the old path's GONE row (finals gone +1); zero conflicts, zero actions | covered: test_seam_reg_standalone_move |
| X19 | fragment winner: right splits fragment {P3,P4} out of base pool N; left independently creates P3, different content | CREATE_CREATE keyed by the fragment; pins the FRAGMENT arm of the consultation (parent lineage for on-lineage, side table for the compare object) | covered: test_seam_fragment_winner |

## Apply copy primitives matrix (CP) -- object copy, logical xattrs

Plotted 2026-08-24, right after the xattr-dir-helpers commit
(e7e5452e1) and BEFORE any of its tests can exist: the helpers
are static functions with no callers until apply-additions wires
them to the action list, so nothing here is observable through
the ioctl yet. Every row is therefore "planned": it names the
intended test and what it needs, and flips to covered when
apply-additions' test pass lands. No skeleton test code is
mocked out ahead of that -- written-but-never-executed
enforcement is where bodies live (the retrospective-7 rule), so
test code arrives only when it can actually run and fail.

Dimensions: source object kind {plain file, directory, symlink
with SA-resident target, symlink with data-block target, device
node}; data shape {empty, single block, multi-block}; attribute
presence {standard set, with ACL, optional attributes absent};
xattr source form {none, SA-only, directory-only, mixed};
destination xattr= mode {sa, dir, off}; value size class {small,
per-entry overflow past DXATTR_MAX_ENTRY_SIZE, aggregate
overflow past DXATTR_MAX_SA_SIZE, zero-length}; fidelity
properties (parent override, xattr-state-free copy, owner
stamping); faults.

Observation mechanism, once apply-additions lands: inspect the
APPLIED left head directly -- rt_open it, walk dirents, read SA
attributes and data, and read xattrs logically. New accessors the
apply harness pass owes this matrix: rt_get_sa_u64 (read one
attribute), rt_sa_absent (assert an attribute is NOT present),
rt_xattr_read (logical per-name value read merging both physical
forms, mirroring the engine's gatherer), rt_xattr_forms (report
which physical forms an object carries: DXATTR, directory, both,
neither), and rt_read_data (content compare). The strongest
single check is the round-trip: re-run the DIFF engine with the
applied result on one side and assert silence -- recorded as this
family's acceptance cell (CP24).

One dimension is documented as unrepresentable rather than
celled: SOURCE AND DESTINATION SA TABLES THAT GENUINELY DIFFER.
The per-side-table parameters exist because attribute numbers are
registered per objset (the sprint-1 code used one table for both,
a real latent bug), but every dataset a fixture can build
registers the identical layout -- the same zfs version registers
the same attributes in the same order -- so no in-harness fixture
can make the tables diverge. The rule is held by the signatures
themselves and by review; it would first bite (and first be
testable) across pool-version skew, which is upstream-review
territory, not harness territory.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| CP1  | plain file, multi-block data, standard attributes | copied object: right DMU kind, byte-identical data, ZPL_PARENT = destination parent, links/times/mode/uid/gid carried | covered: test_apply_copy_file |
| CP2  | empty file (no data blocks) | size 0, no data-copy loop entered, attributes intact | covered: test_apply_copy_empty |
| CP3  | directory | ZAP object allocated, directory attributes carried; child recursion is apply-additions' own cell, not this one | covered: test_apply_copy_dir |
| CP4  | symlink, SA-resident target | ZPL_SYMLINK carried; target readable and equal | covered: test_apply_copy_symlink_sa |
| CP5  | symlink, data-block target (long target) | target crosses via the data copy | planned: test_apply_copy_symlink_data |
| CP6  | device node | ZPL_RDEV carried; plain-file dnode kind | covered: test_apply_copy_device |
| CP7  | file with ACL | ZPL_DACL_ACES + ZPL_DACL_COUNT both cross | planned: test_apply_copy_acl |
| CP8  | optional attributes ABSENT on source (no RDEV/PROJID) | destination has them ABSENT too -- presence-conditional copy never invents defaults (the sprint-1 correction this family exists to pin) | covered: test_apply_no_invented_attrs |
| CP9  | multi-link source file, one COPY action | the HELPER copies ZPL_LINKS as-is; apply-additions must overwrite with merged-namespace truth -- the cell lives in apply-additions' matrix and is cross-referenced here so neither matrix drops it | planned: apply-additions matrix owns it |
| CP10 | src/dst SA table skew | unrepresentable in-harness (see preamble); held by signatures + review | documented above |
| CP11 | no xattrs on source | destination carries neither xattr form | covered: test_apply_xattr_none |
| CP12 | SA-only source, dst xattr=sa, small values | SA-resident on destination; logical set round-trips | planned: test_apply_xattr_sa_to_sa |
| CP13 | DIRECTORY-only source, dst xattr=sa, small values | representation CONVERTS to SA-resident -- the destination-form decision's core: never replicate the source's form | covered: test_apply_xattr_dir_to_sa |
| CP14 | SA-only source, dst xattr=dir | converts to a hidden directory; directory stamped 0041777 + ZFS_XATTR + links 2 + size 2+n, children stamped regular + ZFS_XATTR | covered: test_apply_xattr_dir_form (destination default is the directory form; the sa-source half rides rt_set_dxattr fixtures when CP15 lands) |
| CP15 | MIXED source (SA and directory at once), dst xattr=sa | gatherer merges both forms into one logical set; all small values land SA | planned: test_apply_xattr_mixed_merge |
| CP16 | one value past DXATTR_MAX_ENTRY_SIZE under xattr=sa | that value overflows to the directory, the rest stay SA -- BOTH forms on one object, as ZPL leaves it; round-trip equal | covered: test_apply_xattr_entry_overflow |
| CP17 | aggregate past DXATTR_MAX_SA_SIZE (many small values) | some values spill to the directory; assert the PROPERTY, not the split: round-trip equal, both forms present, packed SA size within the cap (which entries spill follows nvlist order and is not contract) | planned: test_apply_xattr_total_overflow |
| CP18 | zero-length xattr value | legal; survives the round-trip in whichever form | covered: inside test_apply_xattr_dir_form (a zero-length value in the fixture) |
| CP19 | dst xattr=off, source HAS xattrs | EOPNOTSUPP from the writer -- the policy hook; what apply turns it into (conflict, warning, abort) is apply-additions' policy cell | covered: test_apply_xattr_off (EOPNOTSUPP propagates, apply fails, rollback restores; the v1 policy is refusal) |
| CP20 | dst xattr=off, source has NO xattrs | clean no-op: the empty-set short-circuit runs before the mode gate | covered: inside test_apply_xattr_off (control file without xattrs applies cleanly first) |
| CP21 | source file owned by nonzero uid/gid, dir-form write | xattr directory and value children carry the OWNING file's uid/gid (no caller credential exists in a sync task) | planned: test_apply_xattr_owner (fixture sets ZPL_UID/GID on source) |
| CP22 | rebase_free_xattr_dir on a populated dir; on an empty dir | directory and every child unallocated afterward (dmu_object_info ENOENT) | covered (delete half): test_apply_unlink_xattr_file frees a populated directory through the unlink path; the replace half (clearing before rewrite) stays planned for apply-edits |
| CP23 | corrupt DXATTR blob on the copy SOURCE | EIO through the gatherer -- rebase_copy_xattrs is the gatherer's third caller; H36 and U9 pinned the walk and phase-C readers, this pins the apply reader | covered: test_apply_corrupt_xattr_source (AP11) |
| CP24 | ACCEPTANCE: apply a right-side object, then re-run the diff engine against the applied result | silence -- no content, attribute, or xattr differences; the copy is invisible to the engine's own eyes | covered: test_apply_roundtrip_acceptance (AP13) |

## Apply matrix (AP) -- applied-state inspection, crashes, cancels

Plotted 2026-08-24, before its tests, covering the apply surface
for additions and deletions plus the failure contract. This is
the pass that pays the inspection debt: until now no test read
the applied HEAD back, so a copy writing garbage would have gone
green. The observation mechanism is direct post-apply inspection
of the left HEAD through new accessors -- rt_get_sa_u64,
rt_sa_absent, rt_read_data, rt_object_exists, rt_xattr_read,
rt_xattr_forms, rt_read_symlink, rt_dir_lookup -- plus the apply
tally line via rt_apply_stats, the fence helpers
(rt_fence_exists, rt_rollback_to_fence, rt_open_snap), and the
engine's injection tunables (rebase_apply_inject_stop_after,
rebase_apply_inject_skip_rollback) driven through
rt_apply_inject.

Dimensions: applied-state fidelity {data, attributes, absent
attributes, xattr forms and values, link counts, object
liveness}; deletion shapes {standalone file, directory tree,
pool member, dead pool, xattr satellite}; the move handoff
(nlink parked at zero between phases); interruption {user cancel
= injected stop with automatic rollback, crash = injected stop
with rollback suppressed, real mid-apply failure}; recovery {the
fence as rollback anchor and revert point}; the tally line.

CP rows flipped to covered by this pass are re-dispositioned in
the CP table itself; AP cells below cover the driver, deletions,
and interruption surface the CP family never claimed.

| Cell | Scenario | Expect | Disposition |
|------|----------|--------|-------------|
| AP1  | right deletes a standalone file | dirent gone, object unallocated, base data untouched elsewhere | covered: test_apply_unlink_file |
| AP2  | right deletes a directory tree | children and directory all gone, objects unallocated (bottom-up order exercised) | covered: test_apply_unlink_tree |
| AP3  | right unlinks one member of a base pool | that dirent gone, object ALIVE with ZPL_LINKS decremented, surviving member's name and data intact | covered: test_apply_unlink_pool_member |
| AP4  | right unlinks EVERY member, left silent (dead pool, no edit) | all dirents gone, object freed | covered: test_apply_unlink_dead_pool |
| AP5  | right deletes a file carrying an xattr directory | file, hidden directory, and every value child unallocated | covered: test_apply_unlink_xattr_file (flips CP22's file half; the apply-edits replace half stays with that issue) |
| AP6  | right renames a standalone file (the LINK-phase handoff) | old name gone, new name NOT yet present, object alive with ZPL_LINKS 0 -- this cell's expectation CHANGES when apply-structural lands (new name present, links 1) and must be rewritten then | covered: test_apply_move_handoff |
| AP7  | USER CANCEL: three copies pending, injected stop after 1 | EINTR; automatic rollback ran: NO addition present, pre-state intact, fence present | covered: test_apply_cancel_rollback |
| AP8  | CRASH: same fixture, stop after 1 with rollback suppressed | EINTR; HEAD is PARTIAL (exactly the first action's file present -- pins list-order determinism too); fence present; manual rollback-to-fence then restores pre-state exactly (pioneers the abort flow) | covered: test_apply_crash_partial |
| AP9  | cancel inside the SECOND pass: one copy + one unlink, stop after 1 | the copy lands, the unlink loop stops, EINTR, rollback restores both (added file gone, deleted file back) | covered: test_apply_cancel_unlinks |
| AP10 | the fence is the revert point | after a fully successful apply, the fence snapshot still exists and READS as the pre-apply state (deleted file present in it, added file absent) | covered: test_apply_fence_content |
| AP11 | real mid-apply failure (not injected): corrupt DXATTR blob on the copy source | EIO through the gatherer (flips CP23), rollback restores the HEAD, fence present | covered: test_apply_corrupt_xattr_source |
| AP12 | the tally line | copies/unlinks/deferred buckets match the fixture arithmetic | covered: test_apply_stats_line (rt_apply_stats) |
| AP13 | acceptance: apply, clear the fence, re-run the rebase | second run sees convergent adds and left-changed paths: zero conflicts, zero copies, zero unlinks -- the applied result is invisible to the engine's own diff (flips CP24; also the first fence-lifecycle exercise: the second run only proceeds because the test destroyed the fence, rehearsing finish/abort) | covered: test_apply_roundtrip_acceptance |
