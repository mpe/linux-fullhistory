#
# alpha/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

NM := nm -B

LINKFLAGS = -static -T arch/alpha/vmlinux.lds
CFLAGS := $(CFLAGS) -pipe -mno-fp-regs -ffixed-8

HEAD := arch/alpha/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/alpha/kernel arch/alpha/mm arch/alpha/lib \
	arch/alpha/math-emu
CORE_FILES := arch/alpha/kernel/kernel.o arch/alpha/mm/mm.o $(CORE_FILES)
LIBS := $(TOPDIR)/arch/alpha/math-emu/math-emu.a \
	$(TOPDIR)/arch/alpha/lib/lib.a $(LIBS) $(TOPDIR)/arch/alpha/lib/lib.a

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

rawboot:
	@$(MAKEBOOT) rawboot

#
# my boot writes directly to a specific disk partition, I doubt most
# people will want to do that without changes..
#
msb my-special-boot:
	@$(MAKEBOOT) msb

bootimage:
	@$(MAKEBOOT) bootimage

srmboot:
	@$(MAKEBOOT) srmboot

archclean:
	@$(MAKEBOOT) clean

archdep:
	@$(MAKEBOOT) dep

bootpfile:
	@$(MAKEBOOT) bootpfile
