#
# Makefile for ia64-specific kdb files..
#
#  Copyright 1999, Silicon Graphics Inc.
#
#  Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
#  Code for IA64 written by Goutham Rao <goutham.rao@intel.com> and
#  Sreenivas Subramoney <sreenivas.subramoney@intel.com>
#

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = kdb.a
L_OBJS  = kdbsupport.o kdb_io.o kdb_bt.o kdb_traps.o

include $(TOPDIR)/Rules.make
