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

AS86    =$(CROSS_COMPILE)as86 -0 -a
AS386   =$(CROSS_COMPILE)as86 -3
LD86    =$(CROSS_COMPILE)ld86 -0

#
# ZIMAGE_OFFSET is the load offset of the compression loader
# BZIMAGE_OFFSET is the load offset of the high loaded compression loader
#
ZIMAGE_OFFSET=0x1000
BZIMAGE_OFFSET=0x100000

#
# IMAGE_OFFSET is the load offset of the _real_ kernel, soon
# to be offset by another 0xC0000000...
#
IMAGE_OFFSET=0xC0100000

# This is used for ELF - it needs to migrate or be moved.
LD_RFLAG = -m elf_i386

LD=$(CROSS_COMPILE)ld -m elf_i386
CPP=$(CC) -E -D__ELF__
OBJDUMP=$(CROSS_COMPILE)objdump
OBJDUMP_FLAGS=-k -q 
ENCAPS=$(CROSS_COMPILE)encaps
OBJCOPY=$(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -R .stab -R .stabstr
ZLDFLAGS=-e startup_32 
LDFLAGS=-e stext
ZLINKFLAGS =-Ttext $(ZIMAGE_OFFSET) $(ZLDFLAGS)
BZLINKFLAGS =-Ttext $(BZIMAGE_OFFSET) $(ZLDFLAGS)
LINKFLAGS =-Ttext $(IMAGE_OFFSET) $(LDFLAGS)

CFLAGS := $(CFLAGS) -pipe

ifdef CONFIG_M386
CFLAGS := $(CFLAGS) -m386 -DCPU=386
endif

ifdef CONFIG_M486
CFLAGS := $(CFLAGS) -m486 -DCPU=486
endif

ifdef CONFIG_M586
CFLAGS := $(CFLAGS) -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=586
endif

ifdef CONFIG_M686
CFLAGS := $(CFLAGS) -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=686
endif

ifdef SMP
CFLAGS := $(CFLAGS) -D__SMP__
endif

HEAD := arch/i386/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/i386/kernel arch/i386/mm arch/i386/lib
ARCHIVES := arch/i386/kernel/kernel.o arch/i386/mm/mm.o $(ARCHIVES)
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
	@$(MAKEBOOT) install

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean

archdep:
	@$(MAKEBOOT) dep
