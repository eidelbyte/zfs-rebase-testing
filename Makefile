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
SRCS	= rebase_test.c

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

OBJS	= rebase_test.lo

all: $(PROG)

$(OBJS): $(SRCS)
	$(COMPILE) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(PROG): $(OBJS)
	$(LINK) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -rf $(PROG) *.o *.lo .libs

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

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -f $(PROG) *.o

.endif
