#
# alpha/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

LINKFLAGS = -non_shared -T 0xfffffc0000304000 -N
CFLAGS := $(CFLAGS) -mno-fp-regs

HEAD := arch/alpha/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/alpha/kernel arch/alpha/lib
ARCHIVES := arch/alpha/kernel/kernel.o $(ARCHIVES)
LIBS := arch/alpha/lib/lib.a $(LIBS)

archclean:

archdep:
