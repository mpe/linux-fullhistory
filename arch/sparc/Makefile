# $Id: Makefile,v 1.21 1996/11/13 05:09:12 davem Exp $
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

INITOBJ = $(TOPDIR)/arch/sparc/kernel/initobj.o
FINITOBJ = $(TOPDIR)/arch/sparc/kernel/finitobj.o

ifdef CONFIG_AP1000
SUBDIRS := $(SUBDIRS) arch/sparc/ap1000
ARCHIVES := $(TOPDIR)/arch/sparc/ap1000/ap1000lib.o $(ARCHIVES) 
DRIVERS := $(DRIVERS) drivers/ap1000/ap1000.a
endif

archclean:
	rm -f $(TOPDIR)/arch/sparc/boot/boot

archdep:
