# $Id: Makefile,v 1.9 1997/03/14 21:04:39 jj Exp $
# sparc64/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# 64-bit Sparc.
#
# Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
#

# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

CC		= sparc64-linux-gcc -D__KERNEL__ -I$(TOPDIR)/include
AS		= sparc64-linux-as
LD		= sparc64-linux-ld
NM		= sparc64-linux-nm
AR		= sparc64-linux-ar
RANLIB		= sparc64-linux-ranlib

#
# Uncomment the first CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7 
CFLAGS := $(CFLAGS) -pipe -fcall-used-g5 -fcall-used-g7

LINKFLAGS = -T arch/sparc64/vmlinux.lds

HEAD := arch/sparc64/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/sparc64/kernel arch/sparc64/lib arch/sparc64/mm \
	arch/sparc64/prom

CORE_FILES := arch/sparc64/kernel/kernel.o arch/sparc64/mm/mm.o $(CORE_FILES)

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc64/prom/promlib.a \
	$(TOPDIR)/arch/sparc64/lib/lib.a

archclean:

archdep:

check_asm:
	$(MAKE) -C arch/sparc64/kernel check_asm
