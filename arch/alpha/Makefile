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

ifdef CONFIG_CROSSCOMPILE
# enable this for linking under OSF/1:
LINKFLAGS = -non_shared -T 0xfffffc0000310000 -N
else
# enable this for linking under Linux:
LINKFLAGS = -static -T arch/alpha/vmlinux.lds -N
# GNU gcc/cc1/as can use pipes instead of temporary files
CFLAGS := $(CFLAGS) -pipe
endif

CFLAGS := $(CFLAGS) -mno-fp-regs

HEAD := arch/alpha/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/alpha/kernel arch/alpha/mm arch/alpha/lib
ARCHIVES := arch/alpha/kernel/kernel.o arch/alpha/mm/mm.o $(ARCHIVES)
LIBS := $(TOPDIR)/arch/alpha/lib/lib.a $(LIBS) $(TOPDIR)/arch/alpha/lib/lib.a

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

#
# my boot writes directly to a specific disk partition, I doubt most
# people will want to do that without changes..
#
msb my-special-boot:
	@$(MAKEBOOT) msb

archclean:
	@$(MAKEBOOT) clean

archdep:
	@$(MAKEBOOT) dep
