# $Id: Makefile,v 1.24 1997/10/02 16:31:16 jj Exp $
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
ELFTOAOUT	= elftoaout

#
# Uncomment the first CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7 
CFLAGS := $(CFLAGS) -pipe -mno-fpu -mtune=ultrasparc -mmedlow \
	-ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare

LINKFLAGS = -T arch/sparc64/vmlinux.lds

HEAD := arch/sparc64/kernel/head.o arch/sparc64/kernel/init_task.o

SUBDIRS := $(SUBDIRS) arch/sparc64/kernel arch/sparc64/lib arch/sparc64/mm \
	arch/sparc64/prom

ifneq ($(CONFIG_SOLARIS_EMUL),n)
  SUBDIRS += arch/sparc64/solaris
endif

ifneq ($(CONFIG_MATHEMU),n)
  SUBDIRS += arch/sparc64/math-emu
endif

CORE_FILES := arch/sparc64/kernel/kernel.o arch/sparc64/mm/mm.o $(CORE_FILES)

ifeq ($(CONFIG_SOLARIS_EMUL),y)
  CORE_FILES += arch/sparc64/solaris/solaris.o
endif

ifeq ($(CONFIG_MATHEMU),y)
  CORE_FILES += arch/sparc64/math-emu/math-emu.o
endif

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc64/prom/promlib.a \
	$(TOPDIR)/arch/sparc64/lib/lib.a

vmlinux.aout: vmlinux
	$(ELFTOAOUT) -o $(TOPDIR)/vmlinux.aout $(TOPDIR)/vmlinux

archclean:
	rm -f $(TOPDIR)/vmlinux.aout

archdep:

check_asm:
	$(MAKE) -C arch/sparc64/kernel check_asm

tftpboot.img:
	$(MAKE) -C arch/sparc64/boot tftpboot.img
