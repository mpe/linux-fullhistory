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
# -zmagic:   
#
# ZLINKFLAGS   = -Ttext 0x1000
# LINKFLAGS    = -Ttext 0x100000
#
# -qmagic (we need to remove the 32 byte header for bootup purposes)
#
ZLINKFLAGS =-qmagic -Ttext 0xfe0
LINKFLAGS =-qmagic -Ttext 0xfffe0
CFLAGS := $(CFLAGS) -pipe

ifdef CONFIG_M486
CFLAGS := $(CFLAGS) -m486
else
ifdef CONFIG_M586
CFLAGS := $(CFLAGS) -mpentium
else
CFLAGS := $(CFLAGS) -m386
endif
endif

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

zImage: vmlinux
	@$(MAKEBOOT) zImage

compressed: zImage

zlilo: vmlinux
	@$(MAKEBOOT) zlilo

archclean:
	@$(MAKEBOOT) clean

archdep:
	@$(MAKEBOOT) dep
