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
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
#

KERNELLOAD	=0xc0000000

ifeq ($(shell uname -m),ppc)
CHECKS		= checks
endif

ASFLAGS		=
LINKFLAGS	= -T arch/ppc/vmlinux.lds -Ttext $(KERNELLOAD) -Bstatic
CFLAGSINC	= -D__KERNEL__ -I$(TOPDIR)/include -D__powerpc__
CFLAGS		:= $(CFLAGS) -D__powerpc__ -fsigned-char -msoft-float -pipe \
		-fno-builtin -ffixed-r2 -Wno-uninitialized -mmultiple -mstring
CPP		= $(CC) -E $(CFLAGS)

ifdef CONFIG_8xx
CFLAGS := $(CFLAGS) -mcpu=860
endif


HEAD := arch/ppc/kernel/head.o

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm arch/ppc/lib
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(ARCHIVES)
CORE_FILES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(CORE_FILES)

ifdef CONFIG_XMON
SUBDIRS += arch/ppc/xmon
CORE_FILES += arch/ppc/xmon/x.o
endif

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot
MAKECOFFBOOT = $(MAKE) -C arch/$(ARCH)/coffboot
MAKECHRPBOOT = $(MAKE) -C arch/$(ARCH)/chrpboot

ifdef CONFIG_8xx
SUBDIRS += arch/ppc/8xx_io
DRIVERS += arch/ppc/8xx_io/8xx_io.a drivers/net/net.a
endif

ifdef CONFIG_APUS
SUBDIRS += arch/ppc/amiga
ARCHIVES += arch/ppc/amiga/amiga.o
CORE_FILES += arch/ppc/amiga/amiga.o
endif

checks:
	@$(MAKE) -C arch/$(ARCH)/kernel checks

BOOT_TARGETS = netboot znetboot zImage floppy install \
	vmlinux.coff znetboot.initrd zImage.initrd vmlinux.coff.initrd

$(BOOT_TARGETS): $(CHECKS) vmlinux
	@$(MAKECOFFBOOT) $@
	@$(MAKEBOOT) $@
	@$(MAKECHRPBOOT) $@

pmac_config:
	rm -f .config arch/ppc/defconfig
	ln -s pmac_defconfig arch/ppc/defconfig

prep_config:
	rm -f .config arch/ppc/defconfig
	ln -s prep_defconfig arch/ppc/defconfig

chrp_config:
	rm -f .config arch/ppc/defconfig
	ln -s chrp_defconfig arch/ppc/defconfig

common_config:
	rm -f .config arch/ppc/defconfig
	ln -s common_defconfig arch/ppc/defconfig

mbx_config:
	rm -f .config arch/ppc/defconfig
	ln -s mbx_defconfig arch/ppc/defconfig

apus_config:
	rm -f .config arch/ppc/defconfig
	ln -s apus_defconfig arch/ppc/defconfig

tags:
	etags */*.c include/{asm,linux}/*.h arch/ppc/kernel/*.{c,h}

archclean:
	rm -f arch/ppc/kernel/{mk_defs,ppc_defs.h,find_name,checks}
	@$(MAKECOFFBOOT) clean
	@$(MAKEBOOT) clean
	@$(MAKECHRPBOOT) clean

archmrproper:

archdep:
	$(MAKEBOOT) fastdep
	$(MAKECHRPBOOT) fastdep

