# $Id: Makefile,v 1.26 1997/03/04 16:26:50 jj Exp $
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
# Uncomment the first CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7
CFLAGS := $(CFLAGS) -pipe -fcall-used-g5 -fcall-used-g7

#LINKFLAGS = -N -Ttext 0xf0004000
LINKFLAGS = -T arch/sparc/vmlinux.lds

HEAD := arch/sparc/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/sparc/kernel arch/sparc/lib arch/sparc/mm \
	arch/sparc/prom

CORE_FILES := arch/sparc/kernel/kernel.o arch/sparc/mm/mm.o $(CORE_FILES)

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc/prom/promlib.a \
	$(TOPDIR)/arch/sparc/lib/lib.a

ifdef CONFIG_AP1000
SUBDIRS := $(SUBDIRS) arch/sparc/ap1000 mpp
CORE_FILES := $(TOPDIR)/arch/sparc/ap1000/ap1000lib.o $(TOPDIR)/mpp/mpplib.o $(CORE_FILES) 
DRIVERS := $(DRIVERS) drivers/ap1000/ap1000.a
CFLAGS := $(CFLAGS) -D__MPP__=1
endif

archclean:

archdep:

check_asm:
	$(MAKE) -C arch/sparc/kernel check_asm
