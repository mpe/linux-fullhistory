# $Id: Makefile,v 1.34 1998/04/06 16:09:34 jj Exp $
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

HEAD := arch/sparc/kernel/head.o arch/sparc/kernel/init_task.o

# Note arch/sparc/mm has to be the last subdir
SUBDIRS := $(SUBDIRS) arch/sparc/kernel arch/sparc/lib arch/sparc/prom \
	arch/sparc/mm

CORE_FILES := arch/sparc/kernel/kernel.o arch/sparc/mm/mm.o $(CORE_FILES)

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc/prom/promlib.a \
	$(TOPDIR)/arch/sparc/lib/lib.a

SUBDIRS += arch/sparc/math-emu
CORE_FILES += arch/sparc/math-emu/math-emu.o

ifdef CONFIG_AP1000
SUBDIRS := $(SUBDIRS) arch/sparc/ap1000 mpp
CORE_FILES := $(TOPDIR)/arch/sparc/ap1000/ap1000lib.o \
	      $(TOPDIR)/mpp/mpplib.o $(CORE_FILES) 
DRIVERS := $(DRIVERS) drivers/ap1000/ap1000.a
CFLAGS := $(CFLAGS) -D__MPP__=1
endif

archclean:
	-$(MAKE) -C arch/sparc/boot archclean
	-$(MAKE) -C arch/sparc/math-emu cleansymlinks

archdep:
	-$(MAKE) -C arch/sparc/math-emu symlinks

check_asm:
	$(MAKE) -C arch/sparc/kernel check_asm

tftpboot.img:
	$(MAKE) -C arch/sparc/boot tftpboot.img

vmlinux.o: $(CONFIGURATION) init/main.o init/version.o linuxsubdirs
	$(LD) -r $(VMLINUX.OBJS) -o vmlinux.o

arch/sparc/boot/btfix.s: arch/sparc/boot/btfixupprep vmlinux.o
	$(OBJDUMP) -x vmlinux.o | arch/sparc/boot/btfixupprep > arch/sparc/boot/btfix.s

arch/sparc/boot/btfix.o: arch/sparc/boot/btfix.s
	$(CC) -c -o arch/sparc/boot/btfix.o arch/sparc/boot/btfix.s

arch/sparc/boot/btfixupprep: arch/sparc/boot/btfixupprep.c
	$(MAKE) -C arch/sparc/boot btfixupprep

vmlinux: arch/sparc/boot/btfix.o
	$(LD) $(LINKFLAGS) vmlinux.o arch/sparc/boot/btfix.o -o vmlinux
	$(NM) vmlinux | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' | sort > System.map
