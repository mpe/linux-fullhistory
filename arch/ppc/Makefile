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
SUFFIX		=.ppc
#AS		= /u/cort/ppc/gcc/ppc-linux-elf/bin/as
AS		= as$(SUFFIX)
ASFLAGS		= 
#LD		= /u/cort/ppc/gcc/ppc-linux-elf/bin/ld
LD		= ld$(SUFFIX)
LINKFLAGS	= -T arch/ppc/ld.script -Ttext 0x90000000 
HOSTCC		= gcc
#CC		= /u/cort/ppc/gcc/bin/gcc
CC		= gcc$(SUFFIX)
#CC 		= /u/cort/ppc/gcc/bin/gcc.ppc
CFLAGS		= -D__KERNEL__ -I$(TOPDIR)/include -D__powerpc__ \
		-Wstrict-prototypes \
		-fomit-frame-pointer \
		-fno-builtin \
		-finhibit-size-directive \
		-O2 -fsigned-char
CPP		= $(CC) -E $(CFLAGS)
#AR		= /u/cort/ppc/gcc/ppc-linux-elf/bin/ar
AR		= ar$(SUFFIX)
#RANLIB		= /u/cort/ppc/gcc/ppc-linux-elf/bin/ranlib
RANLIB		= ar$(SUFFIX)
#STRIP		= /u/cort/ppc/gcc/ppc-linux-elf/bin/strip
STRIP		= strip$(SUFFIX)
#NM		= /u/cort/ppc/gcc/ppc-linux-elf/bin/nm
NM		= nm$(SUFFIX)

#
# NFS_ROOT_NAME specifies the default name of the directory to mount
# as root via NFS, if the kernel does not get the "root=" option from
# the boot loader. The "%s" will be replaced by the IP-number of the
# local system.
#
NFS_ROOT = -DNFS_ROOT="\"/joplin/ppc/root/\""

HEAD := arch/ppc/kernel/head.o

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm arch/ppc/lib
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(ARCHIVES)


MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

tImage: vmlinux
	@$(MAKEBOOT) tImage



arch/ppc/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/kernel

arch/ppc/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/mm

arch/ppc/lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/ppc/lib


archclean:
	/bin/rm -f arch/ppc/*/*.o #arch/ppc/kernel/mk_defs arch/ppc/kernel/ppc_defs.h
	/bin/rm -f arch/ppc/*~ arch/ppc/*/*~ include/asm-ppc/*~ arch/ppc/boot/mkboot

archdep:

corttags :
	etags arch/ppc/*/*.c include/asm-ppc/* drivers/*/*.c net/*.c */*.c
