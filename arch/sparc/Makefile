# $Id: Makefile,v 1.39 1998/09/16 12:31:31 jj Exp $
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

IS_EGCS := $(shell if $(CC) --version 2>&1 | grep 'egcs' > /dev/null; then echo y; else echo n; fi)
NEW_GAS := $(shell if $(LD) --version 2>&1 | grep 'elf64_sparc' > /dev/null; then echo y; else echo n; fi)

ifeq ($(NEW_GAS),y)
AS              := $(AS) -32
LD              := $(LD) -m elf32_sparc
endif

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7
ifneq ($(IS_EGCS),y)
CFLAGS := $(CFLAGS) -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
else
CFLAGS := $(CFLAGS) -m32 -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
endif

#LINKFLAGS = -N -Ttext 0xf0004000
LINKFLAGS = -T arch/sparc/vmlinux.lds

HEAD := arch/sparc/kernel/head.o arch/sparc/kernel/init_task.o

SUBDIRS := $(SUBDIRS) arch/sparc/kernel arch/sparc/lib arch/sparc/prom \
	arch/sparc/mm arch/sparc/math-emu

CORE_FILES := arch/sparc/kernel/kernel.o arch/sparc/mm/mm.o $(CORE_FILES) \
	arch/sparc/math-emu/math-emu.o

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc/prom/promlib.a \
	$(TOPDIR)/arch/sparc/lib/lib.a

ifdef CONFIG_AP1000
SUBDIRS := $(SUBDIRS) arch/sparc/ap1000 mpp
CORE_FILES := $(TOPDIR)/arch/sparc/ap1000/ap1000lib.o \
	      $(TOPDIR)/mpp/mpplib.o $(CORE_FILES) 
DRIVERS := $(DRIVERS) drivers/ap1000/ap1000.a
CFLAGS := $(CFLAGS) -D__MPP__=1
endif

# This one has to come last
SUBDIRS += arch/sparc/boot
CORE_FILES_NO_BTFIX := $(CORE_FILES)
CORE_FILES += arch/sparc/boot/btfix.o

archclean:
	rm -f $(TOPDIR)/vmlinux.aout
	-$(MAKE) -C arch/sparc/boot clean

archmrproper:
	-$(MAKE) -C arch/sparc/math-emu cleansymlinks

archdep:
	-$(MAKE) -C arch/sparc/math-emu symlinks

check_asm:
	$(MAKE) -C arch/sparc/kernel check_asm

tftpboot.img:
	$(MAKE) -C arch/sparc/boot tftpboot.img
