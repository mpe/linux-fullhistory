# $Id: Makefile,v 1.2 1996/12/27 17:28:20 davem Exp $
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

#CFLAGS := $(CFLAGS) -g -pipe
CFLAGS := $(CFLAGS) -pipe

LINKFLAGS = -N -Ttext 0xFFFFF80000008000

HEAD := arch/sparc/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/sparc64/kernel arch/sparc64/lib arch/sparc64/mm \
	arch/sparc64/prom

ARCHIVES := arch/sparc64/kernel/kernel.o arch/sparc64/mm/mm.o $(ARCHIVES)

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc64/prom/promlib.a \
	$(TOPDIR)/arch/sparc64/lib/lib.a

INITOBJ = $(TOPDIR)/arch/sparc64/kernel/initobj.o
FINITOBJ = $(TOPDIR)/arch/sparc64/kernel/finitobj.o

archclean:

archdep:
