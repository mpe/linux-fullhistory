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

# GCC 2.7 uses different options to later compilers; sort out which we have
CONFIG_GCC_NEW		:= $(shell if $(CC) --version 2>&1 | grep '^2\.7' > /dev/null; then echo n; else echo y; fi)

# See if this is ld "2.9.4" or later
NEW_LINKER		:= $(shell if $(LD) --gc-sections --version >/dev/null 2>&1; then echo y; else echo n; fi)

# CFLAGS_PROC - processor dependent CFLAGS
# PROCESSOR   - processor type
# TEXTADDR    - Uncompressed kernel link text address
# ZTEXTADDR   - Compressed kernel link text address
# ZRELADDR    - Compressed kernel relocating address
#	        (point at which uncompressed kernel is loaded).

#
# select flags depending on the compiler
#
ifeq ($(CONFIG_GCC_NEW),y)
  CFLAGS_PROC		:= -mshort-load-bytes -msoft-float
  CFLAGS_PROC_CPU_26	:= -mcpu=arm3 -Os
  CFLAGS_PROC_CPU_32v3	:= -march=armv3
  CFLAGS_PROC_CPU_32v4	:= -march=armv4
  CFLAGS_ARM6		:= -mtune=arm6
  CFLAGS_ARM7		:= -mtune=arm7
  CFLAGS_SA110		:= -mtune=strongarm110
else
  CFLAGS_PROC		:=
  CFLAGS_PROC_CPU_26	:= -m3
  CFLAGS_PROC_CPU_32v3	:=
  CFLAGS_PROC_CPU_32v4	:=
  CFLAGS_ARM6		:= -m6
  CFLAGS_ARM7		:= -m6
  CFLAGS_SA110		:= -m6
endif

ifeq ($(NEW_LINKER),y)
  ASFLAGS_PROC		:= -mno-fpu
  ASFLAGS_PROC_CPU_26	:= -mapcs-26
  ASFLAGS_PROC_CPU_32v3	:= -mapcs-32 -marmv3m
  ASFLAGS_PROC_CPU_32v4	:= -mapcs-32 -marmv4t
  LINKFLAGS		:= -p
else
  ASFLAGS_PROC		:=
  ASFLAGS_PROC_CPU_26	:= -m3
  ASFLAGS_PROC_CPU_32v3	:= -m6
  ASFLAGS_PROC_CPU_32v4	:= -m6
  LINKFLAGS		:=
endif

ifeq ($(CONFIG_CPU_26),y)
  PROCESSOR		 = armo
  TEXTADDR		 = 0x02080000
  ZTEXTADDR		 = 0x01800000
  ZRELADDR		 = 0x02080000
  CFLAGS_PROC		+= $(CFLAGS_PROC_CPU_26)
  ASFLAGS_PROC		+= $(ASFLAGS_PROC_CPU_26)
endif

ifeq ($(CONFIG_CPU_32),y)
  PROCESSOR		 = armv
  TEXTADDR		 = 0xC0008000
  ifeq ($(CONFIG_CPU_32v4),y)
    CFLAGS_PROC		+= $(CFLAGS_PROC_CPU_32v4)
    ASFLAGS_PROC	+= $(ASFLAGS_PROC_CPU_32v4)
  else
    CFLAGS_PROC		+= $(CFLAGS_PROC_CPU_32v3)
    ASFLAGS_PROC	+= $(ASFLAGS_PROC_CPU_32v3)
  endif
  #
  # Exactly one of the following must be selected
  #
  ifeq ($(CONFIG_CPU_ARM6),y)
    CFLAGS_PROC		+= $(CFLAGS_ARM6)
  else
  ifeq ($(CONFIG_CPU_ARM7),y)
    CFLAGS_PROC		+= $(CFLAGS_ARM7)
  else
  ifeq ($(CONFIG_CPU_SA110),y)
    CFLAGS_PROC		+= $(CFLAGS_SA110)
  endif
  endif
  endif
endif


COMPRESSED_HEAD	 = head.o

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
ZTEXTADDR	 = 0x10008000
ZRELADDR	 = 0x10008000
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
MACHINE		 = ebsa110
ARCHDIR		 = ebsa110
ZTEXTADDR	 = 0x00008000
ZRELADDR	 = 0x00008000
endif

ifeq ($(CONFIG_FOOTBRIDGE),y)
MACHINE		 = footbridge
ARCHDIR		 = ebsa285
ZTEXTADDR	 = 0x00008000
ZRELADDR	 = 0x00008000
endif

ifeq ($(CONFIG_ARCH_CO285),y)
TEXTADDR	 = 0x60008000
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
MACHINE		 = nexuspci
ARCHDIR		 = nexuspci
ZTEXTADDR	 = 0x40200000
ZRELADDR	 = 0x40008000
COMPRESSED_EXTRA = $(TOPDIR)/arch/arm/lib/ll_char_wr_scc.o
COMPRESSED_HEAD	 = head-nexuspci.o
endif



PERL		 = perl
LD		 = $(CROSS_COMPILE)ld
OBJCOPY		 = $(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
OBJDUMP		 = $(CROSS_COMPILE)objdump
CPP		 = $(CC) -E
ARCHCC		:= $(word 1,$(CC))
GCCLIB		:= `$(CC) $(CFLAGS_PROC) --print-libgcc-file-name`
HOSTCFLAGS	:= $(CFLAGS:-fomit-frame-pointer=)
ifeq ($(CONFIG_FRAME_POINTER),y)
CFLAGS		:= $(CFLAGS:-fomit-frame-pointer=)
endif
CFLAGS		:= $(CFLAGS_PROC) $(CFLAGS) -pipe
ASFLAGS		:= $(ASFLAGS_PROC) $(ASFLAGS)
LINKFLAGS	+= -X -T $(TOPDIR)/arch/arm/vmlinux-$(PROCESSOR).lds -e stext
ZLINKFLAGS	 = -Ttext $(ZTEXTADDR)

# If we're intending to debug the kernel, make sure it has line number
# information.  This gets stripped out when building (z)Image so it doesn't
# add anything to the footprint of the running kernel.
ifeq ($(CONFIG_DEBUG_INFO),y)
CFLAGS		+= -g
endif

HEAD		:= arch/arm/kernel/head-$(PROCESSOR).o \
		   arch/arm/kernel/init_task.o
SUBDIRS		:= arch/arm/special $(SUBDIRS) arch/arm/lib arch/arm/kernel \
		   arch/arm/mm arch/arm/nwfpe
CORE_FILES	:= arch/arm/kernel/kernel.o arch/arm/mm/mm.o $(CORE_FILES)
LIBS		:= arch/arm/lib/lib.a $(LIBS) $(GCCLIB)
DRIVERS		+= arch/arm/special/special.a

ifeq ($(CONFIG_ARCH_ACORN),y)
SUBDIRS		+= drivers/acorn/block drivers/acorn/char drivers/acorn/net \
		   drivers/acorn/scsi
DRIVERS		+= drivers/acorn/block/acorn-block.a \
		   drivers/acorn/char/acorn-char.a \
		   drivers/acorn/net/acorn-net.a \
		   drivers/acorn/scsi/acorn-scsi.a
endif

ifeq ($(CONFIG_NWFPE),y)
DRIVERS		+= arch/arm/nwfpe/math-emu.a
endif

MAKEBOOT	 = $(MAKE) -C arch/$(ARCH)/boot

# The following is a hack to get 'constants.h' up
# to date before starting compilation
CONSTANTS	:= constants

constants: dummy
	@$(MAKE) -C arch/arm/lib constants.h

symlinks: archsymlinks

archsymlinks:
	$(RM) include/asm-arm/arch include/asm-arm/proc
	(cd include/asm-arm; ln -sf arch-$(ARCHDIR) arch; ln -sf proc-$(PROCESSOR) proc)

# We need to rebuild the linker script
# each time, in case the architecture has
# changed.
.PHONY: arch/arm/vmlinux-$(PROCESSOR).lds

vmlinux: arch/arm/vmlinux-$(PROCESSOR).lds

arch/arm/vmlinux-$(PROCESSOR).lds: $(TOPDIR)/arch/arm/vmlinux-$(PROCESSOR).lds.in
	@sed 's/TEXTADDR/$(TEXTADDR)/' <$< >$@

arch/arm/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/kernel

arch/arm/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/mm

arch/arm/lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/lib

zImage zinstall Image install: vmlinux
	@$(MAKEBOOT) $@

# Once we've finished integrating the sources, the @$(MAKE) will disappear
archmrproper:
	rm -f include/asm-arm/arch include/asm-arm/proc
	@$(MAKE) -C arch/$(ARCH)/special mrproper
	rm -f $(TOPDIR)/arch/arm/vmlinux-*.lds

archclean:
	@$(MAKEBOOT) clean
	$(RM) arch/arm/lib/constants.h

archdep:
	@$(MAKEBOOT) dep

# My testing targets (that short circuit a few dependencies)
zImg:;	@$(MAKEBOOT) zImage
Img:;	@$(MAKEBOOT) Image
i:;	@$(MAKEBOOT) install
zi:;	@$(MAKEBOOT) zinstall

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

