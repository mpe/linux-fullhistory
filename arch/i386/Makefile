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

#
# Set these to indicate how to link it..
#
# -zmagic:   (aout, old GCC-2.5.2)
#
# ZLINKFLAGS   = -Ttext 0x1000
# BZLINKFLAGS  = -Ttext 0x100000     # (for big high loaded kernels)
# LINKFLAGS    = -Ttext 0x100000
#
# -qmagic:   (aout)
#
# ZLINKFLAGS   = -Ttext 0x0xfe0
# BZLINKFLAGS  = -Ttext 0xfffe0      # (for big high loaded kernels)
# LINKFLAGS    = -Ttext 0xfffe0
#

AS86    =$(CROSS_COMPILE)as86 -0 -a
AS386   =$(CROSS_COMPILE)as86 -3
LD86    =$(CROSS_COMPILE)ld86 -0

ifdef CONFIG_KERNEL_ELF

LD=$(CROSS_COMPILE)ld -m elf_i386
CPP=$(CC) -E -D__ELF__
OBJDUMP =$(CROSS_COMPILE)objdump
ENCAPS=$(CROSS_COMPILE)encaps
OBJDUMP_FLAGS=-k -q 
ZLDFLAGS=-e startup_32 
LDFLAGS=-e stext
ZIMAGE_OFFSET=0x1000
IMAGE_OFFSET=0x100000
ZLINKFLAGS =-Ttext $(ZIMAGE_OFFSET) $(ZLDFLAGS)
BZLINKFLAGS =-Ttext $(IMAGE_OFFSET) $(ZLDFLAGS)
LINKFLAGS =-Ttext $(IMAGE_OFFSET) $(LDFLAGS)

else

#
# -qmagic (we need to remove the 32 byte header for bootup purposes)
#
ZLINKFLAGS =-qmagic -Ttext 0xfe0
BZLINKFLAGS =-qmagic -Ttext 0xfffe0
LINKFLAGS =-qmagic -Ttext 0xfffe0
endif

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
