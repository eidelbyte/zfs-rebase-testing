# zfs-rebase-testing

Userspace test harness for `dsl_rebase()`. Links against `libzpool` to
exercise the kernel rebase code without booting a kernel or mounting a
filesystem.

## Layout

| File | Contents |
|------|----------|
| `rebase_test.h` | shared declarations, TEST/RT_CHECK macros |
| `rebase_test_main.c` | `main()`, section table, result summary |
| `rt_harness.c` | counters, `rt_open`/`rt_close`, `rt_run_rebase`, walk-stats scraper, manifest inspection |
| `rt_scaffold.c` | pool/vdev lifecycle, scaffolds, snapshots, clones |
| `rt_zpl.c` | raw DMU/ZAP/SA object manipulation helpers |
| `test_basic.c` | single-side standalone changes, error cases (13 tests) |
| `test_setup.c` | setup matrix: discovery, preconditions, fences (12 tests) |
| `test_walk.c` | walk matrix: union iteration, recursion, faults (9 tests) |
| `test_hysteria.c` | hysteria matrix via walk-stats counters (31 tests) + samepath convergence suppression (7 tests) |
| `test_diff.c` | standalone-diff matrix: two-axis records via changelist counts (19 tests) |
| `test_moves.c` | move-collapse matrix via move-stats counters (16 tests) + samepath move conflicts (10 tests) |
| `test_anchor.c` | linkpool-anchor matrix (crossref phases A+B) via anchor/target tallies (17 tests) |
| `test_merge.c` | membership-merge matrix (crossref phases C+D) via finals/conflicts tallies (24 tests) |
| `test_linkpool.c` | linkpool discovery/membership/verify matrix + pool-content conflict cases (10 tests) |
| `test_emit.c` | content-merge-emit matrix (crossref phases E+F) via the manifest (10 tests) |
| `test_crossref.c` | samepath conflict types, benign cases, clean merges (11 tests) |
| `test_seam.c` | cross-domain seam matrix: same-path collisions across the two referees (19 tests) |
| `test_apply.c` | apply matrices (copy primitives, state, edits, structural) with post-apply readback and injection (57 tests) |

265 tests total. Section names double as command-line arguments (see
Running below).

Tests are planned by problem-space matrix: see `TEST-MATRIX.md` for
the methodology, the per-phase matrices, and the cell-to-test
mapping. Each test's comment names the matrix cells it covers.

On a machine with no ZFS headers, `devcheck/syncheck.sh` compiles
every source against a stub header (syntax, call arity, unused
variables, -Wcast-qual) and runs the consistency checks
(defined-vs-called test counts, brace balance, ASCII). It is the
pre-push gate; the FreeBSD build and run remain the authority. Keep
`devcheck/stub/rebase_test.h` in sync when helpers or libzpool
calls are added.

## Prerequisites

An OpenZFS source tree for the headers, and a `libzpool` that contains
`dsl_rebase()`.  There are two ways to get this.

### Option A: FreeBSD base (no extra packages)

If you build ZFS as part of the FreeBSD base system, rebuild the
userspace library after modifying `dsl_rebase.c`:

```sh
# Rebuild just the ZFS userspace libraries
cd /usr/src/cddl/lib/libzpool && make && sudo make install

# Or do a full world rebuild
cd /usr/src && make buildworld && sudo make installworld
```

The source tree at `/usr/src/sys/contrib/openzfs` provides headers;
the installed `/lib/libzpool.so` provides the link target.

### Option B: Standalone autotools build

If you prefer an isolated build (does not touch system libraries),
install autotools from ports and build OpenZFS standalone:

```sh
pkg install autoconf automake libtool

cd /path/to/openzfs
./autogen.sh
./configure
make -j$(sysctl -n hw.ncpu)
```

This produces `libtool`, `.la` archives, and `zfs_config.h` inside
the tree.  The Makefile detects this automatically.

## Building the test harness

```sh
cd /path/to/zfs-rebase-testing

# Option A: against FreeBSD base (headers from source, libs from system)
make ZFS_SRC=/usr/src/sys/contrib/openzfs

# Option B: against an autotools-built tree
make ZFS_SRC=/path/to/openzfs
```

The Makefile auto-detects which mode to use based on whether
`$(ZFS_SRC)/libtool` exists.

- **Option A** produces a `rebase_test` binary directly.
- **Option B** produces it via libtool in `.libs/rebase_test`.

## Running

```sh
sudo ./rebase_test                 # all sections
sudo ./rebase_test moves           # one section
sudo ./rebase_test basic linkpool  # several sections
```

The harness must be run as root (it calls `kernel_init` which
opens `/dev/zfs`).  Each test creates a pool on a file vdev at
`/tmp/rtest_vdev`, runs the test, and destroys the pool.

Sections: `basic`, `setup`, `walk`, `hysteria`, `diff`, `moves`,
`anchor`, `merge`, `emit`, `linkpool`, `crossref`.

A full run against a finished engine ends with:

```
=====================
Results: 265/265 passed
```

As of content-merge-emit (2026-08-23) the diff pipeline is
complete: a successful rebase returns 0 with the summary manifest
in outnvl, and EVERY section is live. The manifest (conflicts,
warnings, counts) is the primary observable from the emit era on;
the walk-summary dbgmsg lines remain byte-stable contracts for the
counter-based sections (hysteria, diff, moves, anchor, merge) as
secondary signals, exactly as their matrix preambles promised.

A test failure prints `FAIL: <reason>` on its line and the program
exits with status 1.

Tests that end in a bare `dsl_rebase(..., NULL)` assert only the
return code (0 for a completed diff; typed errnos for precondition
and corruption cases). Tests that call `rt_run_rebase()` also
inspect the manifest from the output nvlist: typed conflicts and
warnings with paths and alt-path arrays, and the count fields
(nconflicts, nwarnings, nactions, changelist counts).

## How it works

The harness uses `libzpool` to run ZFS kernel code in userspace
(same approach as `ztest` and `zdb`).  Key details:

- **Pool creation**: creates a 128 MiB file vdev, builds an nvlist
  vdev tree, calls `spa_create()`.

- **ZPL dataset creation**: bypasses `zfs_create_fs()` (which depends
  on VFS structures unavailable in userspace) by manually creating
  `MASTER_NODE`, SA master node, delete queue, and root directory
  using raw DMU/ZAP/SA calls.

- **Directory entries**: stored with `ZFS_DIRENT_MAKE(type, obj)`
  encoding to match real ZPL layout.  Reads use `ZFS_DIRENT_OBJ()`
  to extract the object number.

- **SA setup**: `sa_setup()` is called after opening each dataset
  to register the ZPL attribute table (`rt_open()` handles this).

- **Hysterical edits**: `rt_hysterical_edit()` simulates nvim-style
  rename-on-save by allocating a new dnode, copying data, removing
  the old ZAP entry, and adding a new one with the same name.

- **The standard scaffold**: `rt_scaffold_basic()` builds src with
  one file ("hello") and one subdirectory ("subdir/inner"),
  snapshots it as `src@base`, and clones left + right from the
  snapshot.  For custom base layouts use `rt_scaffold_empty_base()`,
  populate src, then `rt_scaffold_snap_and_clone()`.

## Adding new tests

1. Pick the section file that matches the pipeline stage under test
   (or add a new `test_<stage>.c`: add it to `SRCS` in the Makefile,
   declare its runner in `rebase_test.h`, and register it in the
   section table in `rebase_test_main.c`).

2. Write a `static int test_your_thing(void)` following the house
   pattern: `TEST_START`, `RT_CHECK(rt_scaffold_basic(), ...)`,
   mutate datasets through an `rt_ds_t` opened with `rt_open()` and
   released with `rt_close()`, then `rt_sync_pool()`, run the
   rebase, `rt_scaffold_teardown()`, `TEST_EXPECT`, `TEST_PASS()`.

3. Add the call to the file's `run_*_tests()`.

4. Use the helpers: `rt_create_file`, `rt_create_dir`,
   `rt_remove_entry`, `rt_edit_file`, `rt_add_hardlink`,
   `rt_hysterical_edit`, `rt_rename_file`, `rt_dir_lookup`, and the
   `rt_manifest_*` inspectors.

5. Always call `rt_sync_pool()` before running the rebase, and
   `rt_close()` before any `RT_CHECK` so a failure never leaks a
   held dataset.

## Sprint-2 notes

The `setup`, `walk`, and matrix-`linkpool` sections target the
sprint-2 engine and are green from zap-walk-basic onward; the
H-matrix `hysteria` tests are green from hysterical-detect onward;
the D-matrix `diff` tests from standalone-diff onward; the
M-matrix half of `moves` from move-collapse onward; the `anchor`
section from linkpool-anchor onward; and the `merge` section from
membership-merge onward.  The
one-time contract flip landed with content-merge-emit
(2026-08-23): every ENOSYS assertion became 0-with-manifest in a
single pass, the former crossref-era tails came alive as the
samepath (P) family's coverage, and only the two linkpool
hardlink-conflict tests needed rewriting -- to LINKPOOL_CONTENT,
the v2 pool-level conflict that replaced per-member noise. The 14-test catalog (sever-vs-nothing, phantom-conflict
dissolution, index recycling, novel overlap, warnings) arrives with
the testing-framework issue. The ZPL helpers maintain real link
semantics: `rt_add_hardlink` bumps ZPL_LINKS, `rt_remove_entry` and
`rt_hysterical_edit` decrement it (to 0 for a last unlink -- the
pathless dnode then models a delete-queue orphan).
