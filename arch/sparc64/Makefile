# $Id: Makefile,v 1.33 1998/10/19 07:04:02 jj Exp $
# sparc64/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# 64-bit Sparc.
#
# Copyright (C) 1996,1998 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
#

# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

CC		:= sparc64-linux-gcc -D__KERNEL__ -I$(TOPDIR)/include

CC_HAS_ARGS := $(shell if echo "$(CC)" | grep '\(__KERNEL__\| 	\)' > /dev/null; then echo y; else echo n; fi)
IS_EGCS := $(shell if $(CC) --version 2>&1 | grep 'egcs' > /dev/null; then echo y; else echo n; fi)
NEW_GAS := $(shell if $(LD) --version 2>&1 | grep 'elf64_sparc' > /dev/null; then echo y; else echo n; fi)

ifneq ($(CC_HAS_ARGS),y)
MAKEOVERRIDES	:= $(shell echo "$(MAKEOVERRIDES)" | sed 's CC=$(CC) CC=$(CC)\\\ -D__KERNEL__\\\ -I$(TOPDIR)/include ')
override CC	:= $(CC) -D__KERNEL__ -I$(TOPDIR)/include
endif

ifneq ($(NEW_GAS),y)
AS		= sparc64-linux-as
LD		= sparc64-linux-ld
NM		= sparc64-linux-nm
AR		= sparc64-linux-ar
RANLIB		= sparc64-linux-ranlib
else
AS		:= $(AS) -64
LD		:= $(LD) -m elf64_sparc
endif
ELFTOAOUT	= elftoaout

#
# Uncomment the first CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7 
ifneq ($(IS_EGCS),y)
  CFLAGS := $(CFLAGS) -pipe -mno-fpu -mtune=ultrasparc -mmedlow \
	    -ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare
else
  CFLAGS := $(CFLAGS) -m64 -pipe -mno-fpu -mcpu=ultrasparc -mcmodel=medlow \
	    -ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare
endif

# Uncomment this to get spinlock/rwlock debugging on SMP.
# DEBUG_SPINLOCK = 1

ifdef CONFIG_SMP
  ifdef DEBUG_SPINLOCK
    CFLAGS += -DSPIN_LOCK_DEBUG
    AFLAGS += -DSPIN_LOCK_DEBUG
  endif
endif

LINKFLAGS = -T arch/sparc64/vmlinux.lds

HEAD := arch/sparc64/kernel/head.o arch/sparc64/kernel/init_task.o

SUBDIRS := $(SUBDIRS) arch/sparc64/kernel arch/sparc64/lib arch/sparc64/mm \
	arch/sparc64/prom

ifneq ($(CONFIG_SOLARIS_EMUL),n)
  SUBDIRS += arch/sparc64/solaris
endif

SUBDIRS += arch/sparc64/math-emu

CORE_FILES := arch/sparc64/kernel/kernel.o arch/sparc64/mm/mm.o $(CORE_FILES)

ifeq ($(CONFIG_SOLARIS_EMUL),y)
  CORE_FILES += arch/sparc64/solaris/solaris.o
endif

CORE_FILES += arch/sparc64/math-emu/math-emu.o

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc64/prom/promlib.a \
	$(TOPDIR)/arch/sparc64/lib/lib.a

vmlinux.aout: vmlinux
	$(ELFTOAOUT) -o $(TOPDIR)/vmlinux.aout $(TOPDIR)/vmlinux

archclean:
	rm -f $(TOPDIR)/vmlinux.aout

archmrproper:

archdep:

check_asm:
	$(MAKE) -C arch/sparc64/kernel check_asm

tftpboot.img:
	$(MAKE) -C arch/sparc64/boot tftpboot.img
