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
classification/rescue, T membership targets). The remaining crossref
phases get theirs as the v2 engine reaches them (test_moves.c's
manifest-based tests and test_crossref.c predate the methodology and
assert sprint-1 behavior; they will be re-plotted when those engine
issues land).

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
