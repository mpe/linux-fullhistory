#
# arch/mips/Makefile
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
# Copyright (C) 1994, 1995 by Waldorf Electronics,
# written by Ralf Baechle
#

AS		= mips-linux-as
ASFLAGS		= -mips3 -mcpu=r4000
LD		= mips-linux-ld
LINKFLAGS	= -Ttext 0xa0000000
#HOSTCC		= gcc
#
# KERNELBASE is quite useless, but I need it to work
# around a hardware bug in my Wreckstation board. Other people
# would burn that @#!%# thing...
#
CC		= mips-linux-gcc -V 2.5.8 -D__KERNEL__ -I$(TOPDIR)/include
CPP		= $(CC) -E $(CFLAGS)
AR		= mips-linux-ar
RANLIB		= mips-linux-ranlib
STRIP		= mips-linux-strip

CFLAGS := $(CFLAGS) #-pipe

CFLAGS := $(CFLAGS) -DKERNELBASE=0xa0000000

ifdef CONFIG_R4X00
CFLAGS := $(CFLAGS) -Wa,-mips3 -mcpu=r4000 -D__R4000__
endif

HEAD := arch/mips/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/mips/kernel arch/mips/mm
ARCHIVES := arch/mips/kernel/kernel.o arch/mips/mm/mm.o $(ARCHIVES)

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

zImage: vmlinux
	@$(MAKEBOOT) zImage

compressed: zImage

zdisk: vmlinux
	@$(MAKEBOOT) zdisk

archclean:
	@$(MAKEBOOT) clean

archdep:
	@$(MAKEBOOT) dep
