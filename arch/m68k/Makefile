#
# m68k/Makefile
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
# Copyright (C) 1994 by Hamish Macdonald
#

# override top level makefile
ifdef CONFIG_KERNEL_ELF
AS	= as -m68020
LD	= ld -m m68kelf
CC	:= $(CC)
# set up for cross compiling
COMPILE_ARCH = $(shell uname -m)
ifneq ($(COMPILE_ARCH),$(ARCH))
	CROSSDIR=/usr/$(ARCH)-linux
	CC := $(CROSSDIR)/$(CC)
	AS := $(CROSSDIR)/$(AS)
	LD := $(CROSSDIR)/$(LD)
	AR := $(CROSSDIR)/$(AR)
	NM := $(CROSSDIR)/$(NM)
	STRIP := $(CROSSDIR)/$(STRIP)
endif
else
AS	= /usr/m68k-linuxaout/bin/as -m68020 
CC	:= $(CC) -pipe -b m68k-linuxaout
LD	= ld -m m68klinux
endif

#
# Set these to indicate how to link it..
#
# -zmagic:   
#
# LINKFLAGS    = -Ttext 0x100000
#
# -qmagic (we need to remove the 32 byte header for bootup purposes)
#

ifdef CONFIG_KERNEL_ELF
LINKFLAGS = -Ttext 0x1000
else
LINKFLAGS = -qmagic -Ttext 0xFE0
endif
CFLAGS := $(CFLAGS) -pipe

HEAD := arch/m68k/kernel/head.o

SUBDIRS += arch/m68k/kernel arch/m68k/mm arch/m68k/console arch/m68k/lib
#SUBDIRS += arch/m68k/kernel arch/m68k/mm arch/m68k/lib
ARCHIVES := arch/m68k/kernel/kernel.o arch/m68k/mm/mm.o $(ARCHIVES)
LIBS += arch/m68k/lib/lib.a

ifdef CONFIG_AMIGA
ARCHIVES := $(ARCHIVES) arch/m68k/amiga/amiga.o
SUBDIRS := $(SUBDIRS) arch/m68k/amiga
endif

ifdef CONFIG_ATARI
ARCHIVES := $(ARCHIVES) arch/m68k/atari/atari.o
SUBDIRS := $(SUBDIRS) arch/m68k/atari
endif

ifdef CONFIG_MAC
ARCHIVES := $(ARCHIVES) arch/m68k/mac/mac.o
SUBDIRS := $(SUBDIRS) arch/m68k/mac
endif

# add in console.a after {amiga,atari}.o that need it
ARCHIVES := $(ARCHIVES) arch/m68k/console/console.a

ifdef CONFIG_FPSP_040
ARCHIVES := $(ARCHIVES) arch/m68k/fpsp040/fpsp.o
SUBDIRS := $(SUBDIRS) arch/m68k/fpsp040
endif

ifdef CONFIG_IFPSP_060
ARCHIVES := $(ARCHIVES) arch/m68k/ifpsp060/ifpsp.o
SUBDIRS := $(SUBDIRS) arch/m68k/ifpsp060
endif

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

lilo:	vmlinux
	if [ -f $(INSTALL_PATH)/vmlinux ]; then mv -f $(INSTALL_PATH)/vmlinux $(INSTALL_PATH)/vmlinux.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv -f $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat vmlinux > $(INSTALL_PATH)/vmlinux
	cp System.map $(INSTALL_PATH)/System.map
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

bootstrap:
	@$(MAKEBOOT) bootstrap

archclean:
	@$(MAKEBOOT) clean

archdep:
	$(MAKEBOOT) dep
