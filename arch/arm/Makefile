#
# arch/arm/Makefile
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
# Copyright (C) 1995-1999 by Russell King

LD		:= $(CROSS_COMPILE)ld
OBJCOPY		:= $(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
CPP		:= $(CC) -E
PERL		:= perl
LINKFLAGS	:= -p -X -T arch/arm/vmlinux.lds
ARCHCC		:= $(word 1,$(CC))

AFLAGS		+= -mno-fpu
CFLAGS_PIPE	:= -pipe
CFLAGS		:= $(CFLAGS) $(CFLAGS_PIPE)

ifdef CONFIG_FRAME_POINTER
CFLAGS		:= $(CFLAGS:-fomit-frame-pointer=)
endif

ifdef CONFIG_DEBUG_INFO
CFLAGS		+= -g
endif

# Ensure this is ld "2.9.4" or later
NEW_LINKER	:= $(shell if $(LD) --gc-sections --version >/dev/null 2>&1; then echo y; else echo n; fi)

ifneq ($(NEW_LINKER),y)
dummy:;		@echo '*** 2.3 kernels no longer build correctly with old versions of binutils.'
		@echo '*** Please upgrade your binutils to 2.9.5.'
		@false
endif


# GCC 2.7 uses different options to later compilers; sort out which we have
NEW_GCC		:= $(shell if $(CC) --version 2>&1 | grep '^2\.7' > /dev/null; then echo n; else echo y; fi)

#
# select flags depending on the compiler
#
ifeq ($(NEW_GCC),y)
CFLAGS			+= -mshort-load-bytes -msoft-float
CFLAGS_PROC_CPU_26	:= -mcpu=arm3 -Os
CFLAGS_PROC_CPU_32v3	:= -march=armv3
CFLAGS_PROC_CPU_32v4	:= -march=armv4
CFLAGS_ARM6		:= -mtune=arm6
CFLAGS_ARM7		:= -mtune=arm7
CFLAGS_SA110		:= -mtune=strongarm110
else
CFLAGS_PROC_CPU_26	:= -m3
CFLAGS_PROC_CPU_32v3	:=
CFLAGS_PROC_CPU_32v4	:=
CFLAGS_ARM6		:= -m6
CFLAGS_ARM7		:= -m6
CFLAGS_SA110		:= -m6
endif

#
# Select CPU dependent flags
#
ifeq ($(CONFIG_CPU_26),y)
 PROCESSOR	 = armo
 TEXTADDR	 = 0x02080000
 CFLAGS		+= $(CFLAGS_PROC_CPU_26)
 AFLAGS		+= -mapcs-26
endif

ifeq ($(CONFIG_CPU_32),y)
 PROCESSOR	 = armv
 TEXTADDR	 = 0xC0008000
 ifeq ($(CONFIG_CPU_32v4),y)
  CFLAGS	+= $(CFLAGS_PROC_CPU_32v4)
  AFLAGS	+= -mapcs-32 -marmv4
 else
  CFLAGS	+= $(CFLAGS_PROC_CPU_32v3)
  AFLAGS	+= -mapcs-32 -marmv3m
 endif
 #
 # Exactly one of the following must be selected
 #
 ifeq ($(CONFIG_CPU_ARM6),y)
  CFLAGS	+= $(CFLAGS_ARM6)
 else
 ifeq ($(CONFIG_CPU_ARM7),y)
  CFLAGS	+= $(CFLAGS_ARM7)
 else
 ifeq ($(CONFIG_CPU_SA110),y)
  CFLAGS	+= $(CFLAGS_SA110)
 endif
 endif
 endif
endif

GCCLIB		:= $(shell $(CC) $(CFLAGS) --print-libgcc-file-name)

ifeq ($(CONFIG_ARCH_A5K),y)
MACHINE		 = a5k
ARCHDIR		 = arc
endif

ifeq ($(CONFIG_ARCH_ARC),y)
MACHINE		 = arc
ARCHDIR		 = arc
endif

ifeq ($(CONFIG_ARCH_RPC),y)
MACHINE		 = rpc
ARCHDIR		 = rpc
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
MACHINE		 = ebsa110
ARCHDIR		 = ebsa110
endif

ifeq ($(CONFIG_FOOTBRIDGE),y)
MACHINE		 = footbridge
ARCHDIR		 = ebsa285
endif

ifeq ($(CONFIG_ARCH_CO285),y)
TEXTADDR	 = 0x60008000
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
MACHINE		 = nexuspci
ARCHDIR		 = nexuspci
endif

ifeq ($(CONFIG_ARCH_SA1100),u)
MACHINE		 = sa1100
ARCHDIR		 = sa1100
endif

HEAD		:= arch/arm/kernel/head-$(PROCESSOR).o \
		   arch/arm/kernel/init_task.o
SUBDIRS		+= arch/arm/kernel arch/arm/mm arch/arm/lib \
		   arch/arm/special arch/arm/nwfpe
CORE_FILES	:= arch/arm/kernel/kernel.o arch/arm/mm/mm.o $(CORE_FILES)
LIBS		:= arch/arm/lib/lib.a $(LIBS) $(GCCLIB)
DRIVERS		+= arch/arm/special/special.a

ifeq ($(CONFIG_NWFPE),y)
LIBS		:= arch/arm/nwfpe/math-emu.o $(LIBS)
endif

ifeq ($(CONFIG_ARCH_ACORN),y)
SUBDIRS		+= drivers/acorn
DRIVERS		+= drivers/acorn/block/acorn-block.a
DRIVERS		+= drivers/acorn/char/acorn-char.a
DRIVERS		+= drivers/acorn/net/acorn-net.a
DRIVERS		+= drivers/acorn/scsi/acorn-scsi.a
endif

MAKEBOOT	 = $(MAKE) -C arch/$(ARCH)/boot

# The following is a hack to get 'constants.h' up
# to date before starting compilation
CONSTANTS	:= constants

constants: $(TOPDIR)/include/asm-arm/proc-fns.h dummy
	@$(MAKE) -C arch/arm/lib constants.h

symlinks: archsymlinks

archsymlinks:
	$(RM) include/asm-arm/arch include/asm-arm/proc
	(cd include/asm-arm; ln -sf arch-$(ARCHDIR) arch; ln -sf proc-$(PROCESSOR) proc)

vmlinux: arch/arm/vmlinux.lds

arch/arm/vmlinux.lds: arch/arm/vmlinux-$(PROCESSOR).lds.in dummy
	@sed 's/TEXTADDR/$(TEXTADDR)/' <$< >$@

arch/arm/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/kernel

arch/arm/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/mm

arch/arm/lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/lib

zImage zinstall Image install: vmlinux
	@$(MAKEBOOT) $@

archmrproper:
	@$(MAKE) -C arch/$(ARCH)/special mrproper
	$(RM) include/asm-arm/arch include/asm-arm/proc

archclean:
	@$(MAKEBOOT) clean
	$(RM) arch/arm/lib/constants.h arch/arm/vmlinux.lds

archdep:
	@$(MAKEBOOT) dep

# My testing targets (that short circuit a few dependencies)
zImg:;	@$(MAKEBOOT) zImage
Img:;	@$(MAKEBOOT) Image
i:;	@$(MAKEBOOT) install
zi:;	@$(MAKEBOOT) zinstall

#
# Configuration targets.  Use these to select a
# configuration for your architecture
#
a5k_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/a5k arch/arm/defconfig

ebsa110_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/ebsa110 arch/arm/defconfig

footbridge_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/footbridge arch/arm/defconfig

rpc_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/rpc arch/arm/defconfig

brutus_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/brutus arch/arm/defconfig

victor_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/victor arch/arm/defconfig

empeg_config:
	$(RM) arch/arm/defconfig
	cp arch/arm/def-configs/empeg arch/arm/defconfig


