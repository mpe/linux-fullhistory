# $Id: Makefile,v 1.6 1995/11/25 00:57:30 davem Exp $
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

#CFLAGS := $(CFLAGS) -g -pipe
CFLAGS := $(CFLAGS) -pipe

LINKFLAGS = -N -Ttext 0xf0004000

HEAD := arch/sparc/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/sparc/kernel arch/sparc/lib arch/sparc/mm \
	arch/sparc/prom

ARCHIVES := arch/sparc/kernel/kernel.o arch/sparc/mm/mm.o $(ARCHIVES)

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc/prom/promlib.a \
	$(TOPDIR)/arch/sparc/lib/lib.a

aoutimage: vmlinux
	elftoaout -o aoutimage vmlinux

archclean:
	rm -f $(TOPDIR)/arch/sparc/boot/boot

archdep:
