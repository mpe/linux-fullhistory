#
# sparc/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# Sparc.
#
# Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
#


# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

#
# How to link, we send the linker the address at which the text section
# is to start. The prom loads us at 0x0-kernel_size. There is also an
# alias of this address space at 0xf8000000-(0xf8000000+kernel_size) but
# I ignore it and eliminate those mappings during vm initialization and
# just leave the low mapping.
#
LINKFLAGS = -N -Ttext 0x00004000
CFLAGS := $(CFLAGS) -pipe

HEAD := arch/sparc/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/sparc/kernel arch/sparc/lib arch/sparc/mm
ARCHIVES := arch/sparc/kernel/kernel.o arch/sparc/mm/mm.o $(ARCHIVES)
LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc/lib/lib.a

archclean:

archdep:
