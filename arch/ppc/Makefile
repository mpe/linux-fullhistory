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
# Modified by Cort Dougan and Paul Mackerras
#

ifeq ($(CONFIG_PMAC),y)
KERNELBASE	=0xc0000000
else
KERNELBASE	=0x90000000
endif

# PowerPC (cross) tools
ifneq ($(shell uname -m),ppc)
CROSS_COMPILE 	=powerpc-eabi-
endif

ASFLAGS		=
LINKFLAGS	= -T arch/ppc/vmlinux.lds -Ttext $(KERNELBASE) -Bstatic
CFLAGSINC	= -D__KERNEL__ -I$(TOPDIR)/include -D__powerpc__
CFLAGS		= $(CFLAGSINC) \
		-Wall -Wstrict-prototypes -Wno-uninitialized \
		-fno-builtin \
		-fsigned-char \
		-msoft-float \
		-O2 -pipe
CPP		= $(CC) -E $(CFLAGS)

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

ifdef CONFIG_XMON
SUBDIRS += arch/ppc/xmon
CORE_FILES += arch/ppc/xmon/x.o
endif

ifdef CONFIG_PMAC
MAKEBOOT = $(MAKE) -C arch/$(ARCH)/coffboot
else
# PReP systems
MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot
endif

checks:
	@$(MAKE) -C arch/$(ARCH)/kernel checks

BOOT_TARGETS = netboot znetboot zImage floppy install \
	vmlinux.coff znetboot.initrd zImage.initrd

$(BOOT_TARGETS): checks vmlinux
	@$(MAKEBOOT) $@

tags:
	etags */*.c include/{asm,linux}/*.h arch/ppc/kernel/*.{c,h}

archclean:
	rm -f arch/ppc/kernel/mk_defs arch/ppc/kernel/ppc_defs.h
	rm -f arch/ppc/kernel/checks
	find arch/ppc/ -name '*.o' -exec /bin/rm -f '{}' \;
	find arch/ppc/ -name '*~' -exec /bin/rm -f '{}' \;
	find arch/ppc/ -name '*.a' -exec /bin/rm -f '{}' \;
	@$(MAKEBOOT) clean

archdep:
	$(MAKEBOOT) fastdep

