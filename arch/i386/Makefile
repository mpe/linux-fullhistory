#
# i386/Makefile
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
#

LD=$(CROSS_COMPILE)ld -m elf_i386
CPP=$(CC) -E
OBJCOPY=$(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
LDFLAGS=-e stext
LINKFLAGS =-T $(TOPDIR)/arch/i386/vmlinux.lds $(LDFLAGS)

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) $(CFLAGS_PIPE) $(CFLAGS_NSR)

ifdef CONFIG_M386
CFLAGS := $(CFLAGS) -m386 -DCPU=386
endif

ifdef CONFIG_M486
CFLAGS := $(CFLAGS) -m486 -DCPU=486
endif

ifdef CONFIG_M586
CFLAGS := $(CFLAGS) -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=586
endif

ifdef CONFIG_M586TSC
CFLAGS := $(CFLAGS) -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=586
endif

ifdef CONFIG_M686
CFLAGS := $(CFLAGS) -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=686
endif

HEAD := arch/i386/kernel/head.o arch/i386/kernel/init_task.o

SUBDIRS := $(SUBDIRS) arch/i386/kernel arch/i386/mm arch/i386/lib
CORE_FILES := arch/i386/kernel/kernel.o arch/i386/mm/mm.o $(CORE_FILES)
LIBS := $(TOPDIR)/arch/i386/lib/lib.a $(LIBS) $(TOPDIR)/arch/i386/lib/lib.a

ifdef CONFIG_MATH_EMULATION
SUBDIRS := $(SUBDIRS) arch/i386/math-emu
DRIVERS := $(DRIVERS) arch/i386/math-emu/math.a
endif

arch/i386/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/i386/kernel

arch/i386/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/i386/mm

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

zImage: vmlinux
	@$(MAKEBOOT) zImage

bzImage: vmlinux
	@$(MAKEBOOT) bzImage

compressed: zImage

zlilo: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=zImage zlilo

bzlilo: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zlilo

zdisk: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=zImage zdisk

bzdisk: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zdisk

install: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage install

archclean:
	@$(MAKEBOOT) clean

archmrproper:

archdep:
	@$(MAKEBOOT) dep
