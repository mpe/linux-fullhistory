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
# Copyright (C) 1994, 1995 by Ralf Baechle
#

ifdef CONFIG_CPU_LITTLE_ENDIAN
prefix		= mipsel-
oformat		= a.out-mips-little-linux
else
prefix		= mips-
oformat		= a.out-mips-big-linux
endif

ifdef CONFIG_EXTRA_ELF_COMPILER
prefix		:= $(prefix)linuxelf-
else
prefix		:= $(prefix)linux-
endif

AS		= $(prefix)as
LD		= $(prefix)ld
LINKFLAGS	= -N -Ttext 0x80000000
#LINKFLAGS	= -oformat=$(oformat) -N -Ttext 0x80000000
#HOSTCC		= gcc
CC		= $(prefix)gcc -D__KERNEL__ -I$(TOPDIR)/include
CPP		= $(CC) -E $(CFLAGS)
AR		= $(prefix)ar
RANLIB		= $(prefix)ranlib
OBJCOPY		= $(prefix)objcopy
OBJDUMP		= $(prefix)objdump
STRIP		= $(prefix)strip
NM		= $(prefix)nm

#
# The new ELF GCC uses -G0 -mabicalls -fpic as default.  We don't need PIC
# code in the kernel since it only slows down the whole thing.  For the
# old GCC these options are just the defaults.  At some point we might
# make use of global pointer optimizations.
#
ifdef CONFIG_ELF_KERNEL
CFLAGS		:= $(CFLAGS) -G0 -mno-abicalls -fno-pic
LINKFLAGS	+= -T arch/mips/ld.script
endif

ifdef CONFIG_REMOTE_DEBUG
CFLAGS		:= $(CFLAGS) -g
endif

ifdef CONFIG_CPU_R3000
CFLAGS		:= $(CFLAGS) -mcpu=r3000 -mips1
#ASFLAGS		:= $(ASFLAGS) -mcpu=r3000 -mips1
endif
ifdef CONFIG_CPU_R6000
CFLAGS		:= $(CFLAGS) -mcpu=r6000 -mips2
#ASFLAGS		:= $(ASFLAGS) -mcpu=r6000 -mips2
endif
ifdef CONFIG_CPU_R4X00
CFLAGS		:= $(CFLAGS) -D__R4000__ -mcpu=r4400 -mips2
#ASFLAGS		:= $(ASFLAGS) -mcpu=r4400 -mips2
endif
ifdef CONFIG_CPU_R4600
CFLAGS		:= $(CFLAGS) -D__R4000__ -mcpu=r4600 -mips2
#ASFLAGS		:= $(ASFLAGS) -mcpu=r4600 -mips2
endif
ifdef CONFIG_CPU_R8000
CFLAGS		:= $(CFLAGS) -D__R4000__ -mcpu=r8000 -mips2
#ASFLAGS		:= $(ASFLAGS) -mcpu=r8000 -mips2
endif
ifdef CONFIG_CPU_R10000
CFLAGS		:= $(CFLAGS) -D__R4000__ -mcpu=r8000 -mips2
#ASFLAGS		:= $(ASFLAGS) -mcpu=r8000 -mips2
endif

CFLAGS		:= $(CFLAGS) -pipe

HEAD := arch/mips/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/mips/kernel arch/mips/mm arch/mips/lib
ARCHIVES := arch/mips/kernel/kernel.o arch/mips/mm/mm.o $(ARCHIVES)
LIBS := arch/mips/lib/lib.a $(LIBS) arch/mips/lib/lib.a

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

zImage: vmlinux
	@$(MAKEBOOT) zImage

compressed: zImage

zdisk: vmlinux
	@$(MAKEBOOT) zdisk

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean

archdep:
	@$(MAKEBOOT) dep
