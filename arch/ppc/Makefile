#
# ppc/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Modified by Cort Dougan
#
# PowerPC (cross) tools
AS		= /usr/local/bin/as.ppc
ASFLAGS		= 
LD		= /u/cort/ppc-gcc/bin/ld.ppc
#LINKFLAGS	= -T arch/ppc/ld.script -Ttext 0x90000000 -Map vmlinux.map
LINKFLAGS	= -T arch/ppc/ld.script -Ttext 0x90000000 
HOSTCC		= gcc
CC		= /usr/local/bin/gcc.ppc 
CFLAGS		= -D__KERNEL__ -I$(TOPDIR)/include \
		-Wstrict-prototypes \
		-fomit-frame-pointer \
		-fno-builtin \
		-finhibit-size-directive \
		-O2 -pipe
#-Wall
CPP		= $(CC) -E $(CFLAGS)
AR		= /u/cort/ppc-gcc/bin/ar.ppc
RANLIB		= /u/cort/ppc-gcc/bin/ranlib.ppc
STRIP		= /u/cort/ppc-gcc/bin/strip.ppc
NM		= /u/cort/ppc-gcc/bin/nm.ppc

#
# Set these to indicate how to link it..
#
# -zmagic:   
#
# ZLINKFLAGS   = -Ttext 0x1000
# LINKFLAGS    = -Ttext 0x100000
#
# -qmagic (we need to remove the 32 byte header for bootup purposes)
#
## ZLINKFLAGS =-qmagic -Ttext 0xfe0
## LINKFLAGS =-qmagic -Ttext 0xfffe0
## CFLAGS := $(CFLAGS) -pipe

## ifdef CONFIG_M486
## CFLAGS := $(CFLAGS) -m486
## else
## ifdef CONFIG_M586
## CFLAGS := $(CFLAGS) -mpentium
## else
## CFLAGS := $(CFLAGS) -m386
## endif
## endif

HEAD := arch/ppc/kernel/head.o

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o $(ARCHIVES)

## ifdef CONFIG_IBCS
## SUBDIRS := $(SUBDIRS) arch/ppc/ibcs
## DRIVERS := $(DRIVERS) arch/ppc/ibcs/ibcs.o
## endif

## ifdef CONFIG_MATH_EMULATION
## SUBDIRS := $(SUBDIRS) arch/ppc/math-emu
## DRIVERS := $(DRIVERS) arch/ppc/math-emu/math.a
## endif

arch/ppc/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/kernel

arch/ppc/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/mm

## MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot
## 
## zImage: vmlinux
## 	@$(MAKEBOOT) zImage
## 
## compressed: zImage
## 
## zlilo: vmlinux
## 	@$(MAKEBOOT) zlilo
## 
## zdisk: vmlinux
## 	@$(MAKEBOOT) zdisk
## 
## install: vmlinux
## 	@$(MAKEBOOT) install

archclean:
#	@$(MAKEBOOT) clean
	/bin/rm -f arch/ppc/kernel/*.o arch/ppc/kernel/mk_defs arch/ppc/kernel/ppc_defs.h mm/*.o
	/bin/rm -f arch/ppc/kernel/*~ arch/ppc/kernel/*~      

archdep:
#	@$(MAKEBOOT) dep


