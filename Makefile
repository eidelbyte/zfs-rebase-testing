# SPDX-License-Identifier: CDDL-1.0
#
# Makefile for zfs-rebase test harness.
#
# Two build modes, auto-detected:
#
#   1. Autotools-built ZFS tree ($(ZFS_SRC)/libtool exists):
#      Uses libtool and .la archives from the in-tree build.
#        make ZFS_SRC=/path/to/configured-and-built/openzfs
#
#   2. FreeBSD base (no libtool in tree):
#      Links directly against system libzpool.so and friends.
#        make ZFS_SRC=/usr/src/sys/contrib/openzfs
#
# In mode 2, libzpool.so must contain dsl_rebase().  Add dsl_rebase.c
# to KERNEL_C in /usr/src/cddl/lib/libzpool/Makefile, then:
#   cd /usr/src/cddl/lib/libzpool && make && sudo make install

ZFS_SRC ?= ../zfs
SRCTOP  ?= /usr/src

# On a box with no research checkout beside this repo, fall back to
# the FreeBSD base tree automatically -- its signatures are the
# authority for harness builds anyway.  An explicit ZFS_SRC on the
# command line still wins over both defaults.
.if !exists($(ZFS_SRC)/include) && exists($(SRCTOP)/sys/contrib/openzfs/include)
ZFS_SRC = $(SRCTOP)/sys/contrib/openzfs
.endif

CC	= cc

CFLAGS  = -std=c99 -O0 -g
CFLAGS += -Wall -Wextra -Wno-sign-compare -Wno-missing-field-initializers
CFLAGS += -Wno-unused-function -Wno-unused-parameter
CFLAGS += -fno-strict-aliasing -fno-omit-frame-pointer
CFLAGS += -include $(ZFS_SRC)/include/os/freebsd/spl/sys/ccompile.h

CPPFLAGS  = -DDEBUG -UNDEBUG -DZFS_DEBUG
CPPFLAGS += -D_GNU_SOURCE -D_REENTRANT
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CPPFLAGS += -I$(ZFS_SRC)/include
CPPFLAGS += -I$(ZFS_SRC)/lib/libspl/include
CPPFLAGS += -I$(ZFS_SRC)/lib/libspl/include/os/freebsd
CPPFLAGS += -I$(ZFS_SRC)/lib/libzpool/include

PROG	= rebase_test
SRCS	= rebase_test_main.c \
	  rt_harness.c \
	  rt_scaffold.c \
	  rt_zpl.c \
	  test_basic.c \
	  test_setup.c \
	  test_walk.c \
	  test_hysteria.c \
	  test_diff.c \
	  test_moves.c \
	  test_anchor.c \
	  test_merge.c \
	  test_emit.c \
	  test_seam.c \
	  test_apply.c \
	  test_linkpool.c \
	  test_crossref.c

# Sprint-3 M1 smoke: links only the harness runtime, never the
# 265-test battery (whose assertions target the revision-2 engine).
M1_PROG	= m1_smoke
M1_SRCS	= m1_smoke.c rt_harness.c rt_scaffold.c rt_zpl.c

# The .tree fixture suite: for each file under trees/, build its three
# trees on a real pool, run the engine, and check the result against
# the gold the fixture carries.  Shares the harness runtime with the
# smoke and nothing else -- the smoke tracks the engine's milestones,
# this tracks the corpus.
#
#   make tree_suite && sudo ./tree_suite
#   sudo ./tree_suite trees/open-problems/5-rename-rename.tree
#
# Define RT_HAVE_DECIDE_ACCESSOR once libzpool exports a way to read
# the decision record back; until then the suite runs its weaker
# census tier and labels every line CENSUS.  See rt_tree_suite.h.
#
#   make tree_suite CPPFLAGS+=-DRT_HAVE_DECIDE_ACCESSOR
TREE_PROG = tree_suite
TREE_SRCS = tree_suite.c \
	  rt_tree_parse.c \
	  rt_tree_build.c \
	  rt_tree_check.c \
	  rt_decision.c \
	  rt_harness.c \
	  rt_scaffold.c \
	  rt_zpl.c

.PHONY: all clean

# --- Mode detection ---
# If the ZFS tree has a libtool binary, it was built with autotools;
# use libtool to compile and link against .la archives.
# Otherwise, compile directly and link against system shared libs.

.if exists($(ZFS_SRC)/libtool)

# Autotools mode
CPPFLAGS += -include $(ZFS_SRC)/zfs_config.h

LIBTOOL	= $(ZFS_SRC)/libtool
LINK	= $(LIBTOOL) --mode=link --tag=CC $(CC)
COMPILE	= $(LIBTOOL) --mode=compile --tag=CC $(CC)

LIBS  = $(ZFS_SRC)/lib/libzpool/libzpool.la
LIBS += $(ZFS_SRC)/lib/libzfs_core/libzfs_core.la
LIBS += $(ZFS_SRC)/lib/libnvpair/libnvpair.la
LIBS += -lm

OBJS	= $(SRCS:.c=.lo)

all: $(PROG)

.SUFFIXES: .c .lo
.c.lo:
	$(COMPILE) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJS): rebase_test.h

$(PROG): $(OBJS)
	$(LINK) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

M1_OBJS	= $(M1_SRCS:.c=.lo)
$(M1_OBJS): rebase_test.h
$(M1_PROG): $(M1_OBJS)
	$(LINK) $(CFLAGS) -o $@ $(M1_OBJS) $(LIBS)

TREE_OBJS = $(TREE_SRCS:.c=.lo)
$(TREE_OBJS): rebase_test.h rt_tree.h rt_tree_check.h rt_tree_suite.h
$(TREE_PROG): $(TREE_OBJS)
	$(LINK) $(CFLAGS) -o $@ $(TREE_OBJS) $(LIBS)

clean:
	rm -rf $(PROG) $(M1_PROG) $(TREE_PROG) *.o *.lo .libs

.else

# FreeBSD direct-link mode.
# Include paths and flags match /usr/src/cddl/lib/libzpool/Makefile.
CPPFLAGS += -include $(SRCTOP)/sys/modules/zfs/zfs_config.h
CPPFLAGS += -DHAVE_ISSETUGID
CPPFLAGS += -DLIB_ZPOOL_BUILD
CPPFLAGS += -I$(SRCTOP)/sys
CPPFLAGS += -I$(ZFS_SRC)/include/os/freebsd/zfs
CPPFLAGS += -I$(SRCTOP)/cddl/compat/opensolaris/include

LIBS = -lzpool -lzutil -lnvpair -lavl -lspl -lumem -lmd -lz -lpthread

all: $(PROG)

$(PROG): $(SRCS) rebase_test.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRCS) $(LIBS)

$(M1_PROG): $(M1_SRCS) rebase_test.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(M1_SRCS) $(LIBS)

$(TREE_PROG): $(TREE_SRCS) rebase_test.h rt_tree.h rt_tree_check.h \
	    rt_tree_suite.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TREE_SRCS) $(LIBS)

clean:
	rm -f $(PROG) $(M1_PROG) $(TREE_PROG) *.o

.endif
