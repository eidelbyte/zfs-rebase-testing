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

Matrices so far: setup (S), walk (W), linkpool (LP/LV), hysteria (H).
Moves and crossref phases get theirs as the v2 engine reaches them
(their current sections predate the methodology and assert sprint-1
behavior; they will be re-plotted when those engine issues land).

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
| S12 | active scrub/resilver | EBUSY | deferred: needs a way to start and hold a scan from the harness |
| S13 | encryption mismatch left/right | EACCES | deferred: harness datasets are unencrypted; needs encrypted scaffold |
| S14 | non-ZPL dataset (zvol) as a side | ENOTSUP | deferred: needs a DMU_OST_ZVOL creation helper |
| S15 | ZPL version < 5 on a side | ENOTSUP | deferred: harness writes ZPL_VERSION current; needs a downgrade injector |
| S16 | FUID table mismatch | ENOTSUP | deferred: needs a FUID table injector (write ZFS_FUID_TABLES key on one clone) -- cheap, do with the next setup pass |

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
