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
		-Wstrict-prototypes \
		-fomit-frame-pointer \
		-fno-builtin \
		-finhibit-size-directive -fno-strength-reduce\
		-O2 -fsigned-char -pipe -mstring -mmultiple
CPP		= $(CC) -E $(CFLAGS)
AR		= ar$(SUFFIX)
RANLIB		= ranlib$(SUFFIX)
STRIP		= strip$(SUFFIX)
NM		= nm$(SUFFIX)

ifdef CONFIG_603
CFLAGS := $(CFLAGS) -mcpu=603 -DCPU=603
endif

ifdef CONFIG_603e
CFLAGS := $(CFLAGS) -mcpu=603e -DCPU=603e
endif

ifdef CONFIG_604
CFLAGS := $(CFLAGS) -mcpu=604 -DCPU=604
endif

#
# NFS_ROOT_NAME specifies the default name of the directory to mount
# as root via NFS, if the kernel does not get the "root=" option from
# the boot loader. The "%s" will be replaced by the IP-number of the
# local system.
#
NFS_ROOT = -DNFS_ROOT="\"/joplin/ppc/root\""

HEAD := arch/ppc/kernel/head.o

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm arch/ppc/lib
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(ARCHIVES)


MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

netboot: vmlinux
	@$(MAKEBOOT) netboot

znetboot: vmlinux
	@$(MAKEBOOT) znetboot

zImage: vmlinux
	@$(MAKEBOOT) zImage

floppy: vmlinux
	@$(MAKEBOOT) floppy

install: vmlinux
	@$(MAKEBOOT) install

arch/ppc/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/kernel

arch/ppc/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/mm

arch/ppc/lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/lib


archclean:
	rm -f arch/ppc/kernel/mk_defs arch/ppc/kernel/ppc_defs.h TAGS
	@$(MAKEBOOT) clean

archdep:

corttags :
	etags arch/ppc/*/*.c include/asm/* */*.c drivers/*/*.c net/*.c
