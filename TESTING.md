# zfs-rebase-testing

Userspace test harness for `dsl_rebase()`. Links against `libzpool` to
exercise the kernel rebase code (diff + collapse phases) without booting
a kernel or mounting a filesystem.

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
sudo ./rebase_test
```

The harness must be run as root (it calls `kernel_init` which
opens `/dev/zfs`).  Each test creates a pool on a file vdev at
`/tmp/rtest_vdev`, runs the test, and destroys the pool.

Expected output when all tests pass:

```
zfs rebase test suite
=====================

  smoke: no changes on either side                      PASS
  left adds a file, right unchanged                     PASS
  right adds a file, left unchanged                     PASS
  left deletes a file                                   PASS
  left edits a file in-place                            PASS
  hysterical edit (nvim-style, same content)            PASS
  left moves (renames) a file                           PASS
  left adds a hardlink                                  PASS
  both sides add different files                        PASS
  both sides edit same file                             PASS
  nested: edit file inside subdirectory                 PASS
  mixed: add + delete + edit on left                    PASS
  move + edit on left                                   PASS
  error: left == right (same dataset)                   PASS
  error: left is a snapshot                             PASS

=====================
Results: 15/15 passed
```

All passing tests return ENOSYS — this is the expected sentinel
after a successful diff + collapse, because the apply phase is
not yet implemented.

## What the tests exercise

| Test | Operation | What it verifies |
|------|-----------|------------------|
| T1  | No changes | Empty changelists, collapse is no-op |
| T2  | Left adds file | Single-side ADD in left changelist |
| T3  | Right adds file | Single-side ADD in right changelist |
| T4  | Left deletes file | Single-side DELETE in left changelist |
| T5  | Left edits file | Same-dnode COW'd EDIT detection |
| T6  | Hysterical edit | Different dnode, same content → not EDIT |
| T7  | Left renames file | ADD+DELETE same obj → collapses to MOVE |
| T8  | Left adds hardlink | ADD for existing base obj → HARDLINK_ADD |
| T9  | Both add different files | Independent ADDs on both sides |
| T10 | Both edit same file | EDIT on both changelists (conflict detection is issue 9) |
| T11 | Nested edit | File EDIT inside subdirectory |
| T12 | Mixed operations | ADD + DELETE + nested EDIT in one changelist |
| T13 | Move + edit | Rename + content change → MOVE_EDIT |
| T14 | Same dataset error | left == right → EINVAL |
| T15 | Left is snapshot | Snapshot as left → EINVAL |

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
  to register the ZPL attribute table (same approach as the main
  rebase code's `rebase_sa_setup()`).

- **Hysterical edits**: `test_hysterical_edit()` simulates nvim-style
  rename-on-save by allocating a new dnode, copying data, removing
  the old ZAP entry, and adding a new one with the same name.

## Adding new tests

1. Write a `static int test_your_thing(void)` function following the
   existing pattern: `TEST_START`, scaffold, modify datasets,
   `sync_pool`, call `dsl_rebase`, `scaffold_teardown`, `TEST_EXPECT`,
   `TEST_PASS`.

2. Add the call to `run_tests()`.

3. Use the helpers: `test_create_file`, `test_create_dir`,
   `test_remove_entry`, `test_edit_file`, `test_add_hardlink`,
   `test_hysterical_edit`, `dir_lookup_obj`.

4. Always call `sync_pool()` before `dsl_rebase()` to flush
   pending transactions to disk.

5. Always call `scaffold_teardown()` before returning, even on
   failure, to clean up the pool and vdev file.

## Verifying changes to dsl_rebase.c

After modifying the rebase code:

```sh
# Option A: Reinstall the system library
cd /usr/src/cddl/lib/libzpool && make && sudo make install

# Option B: Rebuild the autotools tree
cd /path/to/openzfs && make -j$(sysctl -n hw.ncpu)

# Then rebuild and run the test harness
cd /path/to/zfs-rebase-testing
make clean && make ZFS_SRC=/path/to/openzfs
sudo ./rebase_test
```

A test failure prints `FAIL: <reason>` and the program exits with
status 1.  The failure message identifies which assertion or
operation failed.
