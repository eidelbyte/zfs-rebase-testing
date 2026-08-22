# Rebase test matrices

The methodology: for each engine phase, plot the problem space first --
enumerate the input dimensions, cross them into cells, and give EVERY
cell one of three dispositions:

- **covered**: a test exists; the test's comment names the cell.
- **existing**: an older test already exercises the cell; mapped here.
- **deferred**: not testable yet (missing helper, missing engine
  phase, environment limits), with the reason and the unblocking work.

A cell with no row in these tables is a hole. Rebases are fiendish;
a bug must have nowhere to hide. When a new engine phase lands, its
matrix is added here BEFORE its tests are written, and each test file's
header points back to its matrix section.

Matrices so far: setup (S), walk (W), linkpool (LP/LV). Hysteria,
moves, and crossref phases get theirs as the v2 engine reaches them
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
| W18 | dangling dirent (entry to unallocated obj) | ENOENT | covered: test_walk_dangling_dirent |
| W19 | hardlinked symlink / device node in a linkpool | ENOSYS | deferred: needs rt_create_symlink / rt_create_device helpers (S_IFLNK / S_IFCHR modes); planned with the hysteria matrix, where content comparison also needs them |
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

Note on LV cells: a DEBUG libzpool ASSERTs on the mismatch (panics the
harness) instead of returning EIO; run them against the non-debug
FreeBSD system library.

The four older manifest-inspecting hardlink tests in test_linkpool.c
(edit-both-sides dedup, delete-no-conflict, delete-vs-edit) assert
crossref-era behavior and will be re-plotted into the crossref matrix
when that engine phase lands; until then they fail cleanly via the
defensive manifest accessors.
