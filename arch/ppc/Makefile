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
#

# PowerPC (cross) tools
AS		= as.ppc
ASFLAGS		=
LD		= ld.ppc
#LINKFLAGS	= -T arch/ppc/ld.script -Ttext 0x90000000 -Map vmlinux.map
LINKFLAGS	= -T arch/ppc/ld.script -Ttext 0x90000000
HOSTCC		= gcc
CC		= cc.ppc
CFLAGS		= -D__KERNEL__ -I$(TOPDIR)/include \
		-Wall -Wstrict-prototypes \
		-msoft-float \
		-fomit-frame-pointer \
		-fno-builtin \
		-finhibit-size-directive \
		-fsigned-char \
		-O2
CPP		= $(CC) -E $(CFLAGS)
AR		= ar.ppc
RANLIB		= ranlib.ppc
STRIP		= strip.ppc
NM		= nm.ppc

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

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm arch/ppc/lib
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(ARCHIVES)
NO_RD_ARCHIVES := arch/ppc/kernel/no_ramdisk.o $(ARCHIVES)
ARCHIVES := arch/ppc/kernel/ramdisk.o $(ARCHIVES)

arch/ppc/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/kernel

arch/ppc/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/mm

arch/ppc/lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/lib

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux.no_ramdisk: $(CONFIGURATION) init/main.o init/version.o linuxsubdirs
	$(LD) $(LINKFLAGS) $(HEAD) init/main.o init/version.o \
		$(NO_RD_ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) -o vmlinux.no_ramdisk

## 
zImage: vmlinux
	@$(MAKEBOOT) zImage

tImage: vmlinux
	@$(MAKEBOOT) tImage

bImage: vmlinux.no_ramdisk
	@$(MAKEBOOT) bImage

bdisk: vmlinux.no_ramdisk
	@$(MAKEBOOT) bdisk

## compressed: zImage
## 
## zlilo: vmlinux
## 	@$(MAKEBOOT) zlilo
## 
zdisk: vmlinux
	@$(MAKEBOOT) zdisk
 
## install: vmlinux
## 	@$(MAKEBOOT) install

archclean:
	@$(MAKEBOOT) clean

archdep:
#	@$(MAKEBOOT) dep
