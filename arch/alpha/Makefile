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

#LINKFLAGS = -static -T arch/alpha/vmlinux.lds
#CFLAGS := $(CFLAGS) -pipe -mno-fp-regs -ffixed-8

ifdef CONFIG_CROSSCOMPILE
# enable this for linking under OSF/1:
LINKFLAGS = -non_shared -T 0xfffffc0000310000 -N
else
 elf=$(shell if $(LD) --help | grep elf64alpha >/dev/null; then echo yes; fi)
 ifeq ($(elf),yes)
#   LINKFLAGS = -static -Ttext 0xfffffc0000310000 -N
LINKFLAGS = -static -T arch/alpha/vmlinux.lds
 else
   LINKFLAGS = -static -T arch/alpha/vmlinux.lds -N
 endif
# GNU gcc/cc1/as can use pipes instead of temporary files
CFLAGS := $(CFLAGS) -pipe
endif

CFLAGS := $(CFLAGS) -mno-fp-regs -ffixed-8 -Wno-uninitialized

# determine if we can use the BWX instructions with GAS
$(shell rm -f ./GAS_VER)
$(shell $(AS) --version >& ./GAS_VER)
OLD_GAS := $(shell if cat ./GAS_VER | grep 'version 2.7' > /dev/null; then echo yes; else echo no; fi)
$(shell rm -f ./GAS_VER)

ifneq ($(OLD_GAS),yes)
 CFLAGS := $(CFLAGS) -Wa,-m21164a -DBWX_USABLE

# if PYXIS, then enable use of BWIO space
 ifeq ($(CONFIG_ALPHA_PYXIS),y)
  CFLAGS := $(CFLAGS) -DBWIO_ENABLED
 endif
endif

HEAD := arch/alpha/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/alpha/kernel arch/alpha/mm arch/alpha/lib \
	arch/alpha/math-emu
CORE_FILES := arch/alpha/kernel/kernel.o arch/alpha/mm/mm.o $(CORE_FILES)

ifeq ($(CONFIG_MATHEMU),y)
  CORE_FILES := $(CORE_FILES) arch/alpha/math-emu/math-emu.o
endif

LIBS := $(TOPDIR)/arch/alpha/lib/lib.a $(LIBS) $(TOPDIR)/arch/alpha/lib/lib.a

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
