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
SUFFIX		= 
AS		= as$(SUFFIX)
ASFLAGS		= 
LD		= ld$(SUFFIX)
LINKFLAGS	= -T arch/ppc/ld.script -Ttext 0x90000000 
HOSTCC		= gcc
CC		= gcc$(SUFFIX)
CFLAGSINC	= -D__KERNEL__ -I$(TOPDIR)/include -D__powerpc__
CFLAGS		= $(CFLAGSINC) \
		-Wstrict-prototypes -fomit-frame-pointer \
		-fno-builtin \
		-finhibit-size-directive \
		-O2 -fsigned-char -pipe -ffixed-r2 -mstring -mmultiple -msoft-float
# -fverbose-asm 
CPP		= $(CC) -E $(CFLAGS)
AR		= ar$(SUFFIX)
RANLIB		= ranlib$(SUFFIX)
STRIP		= strip$(SUFFIX)
NM		= nm$(SUFFIX)

ifdef CONFIG_601
CFLAGS := $(CFLAGS) -mcpu=601 -DCPU=601
endif

ifdef CONFIG_603
CFLAGS := $(CFLAGS) -mcpu=603 -DCPU=603
endif

ifdef CONFIG_604
CFLAGS := $(CFLAGS) -mcpu=604 -DCPU=604
endif

HEAD := arch/ppc/kernel/head.o

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm arch/ppc/lib
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(ARCHIVES)
CORE_FILES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(CORE_FILES)

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

checks:
	@$(MAKE) -C arch/$(ARCH)/kernel checks

netboot: checks vmlinux
	@$(MAKEBOOT) netboot

znetboot: checks vmlinux
	@$(MAKEBOOT) znetboot

#rcpboot: checks vmlinux
#	@$(MAKEBOOT) rcpboot

zImage: checks vmlinux
	@$(MAKEBOOT) zImage

floppy: checks vmlinux
	@$(MAKEBOOT) floppy

install: checks vmlinux
	@$(MAKEBOOT) install

vmlinux.coff : checks vmlinux
	$(MAKE) -C arch/ppc/coffboot/ vmlinux.coff

arch/ppc/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/kernel

arch/ppc/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/mm

arch/ppc/lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/lib

diffs:
	arch/ppc/mkdiff

tar:
	arch/ppc/mktar

archclean:
	rm -f arch/ppc/kernel/mk_defs arch/ppc/kernel/ppc_defs.h arch/ppc/kernel/checks TAGS
	rm -f `find arch/ppc/ \( -name '*.[oas]' -o -name '*~' -o -name '#*#' \) -print`
	rm -f `find include/asm-ppc/ \( -name '*.[oas]' -o -name '*~' -o -name '#*#' \) -print`
	@$(MAKEBOOT) clean

archdep:
	$(MAKE) -C arch/ppc/boot fastdep
	$(MAKE) -C arch/ppc/kernel fastdep
	$(MAKE) -C arch/ppc/mm fastdep
	$(MAKE) -C arch/ppc/lib fastdep

tags :
	etags arch/ppc/*/*.c arch/ppc/*/*.S include/asm/* */*.c 

