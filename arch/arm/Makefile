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

CFLAGS_PROC		:=
ASFLAGS_PROC		:=

# GCC 2.7 uses different options to later compilers; sort out which we have
CONFIG_GCC_NEW		:= $(shell if $(CC) --version 2>&1 | grep '^2\.7' > /dev/null; then echo n; else echo y; fi)

# Hack to get around RiscPC with StrongARM optimistaion
# problem - force ARM710 optimisation for now.
ifeq ($(CONFIG_GCC_NEW),y)
  ifeq ($(CONFIG_ARCH_RPC),y)
    ifeq ($(CONFIG_CPU_SA110),y)
      CONFIG_CPU_SA110	:= n
      CONFIG_CPU_ARM7	:= y
    endif
  endif
endif

ifeq ($(CONFIG_CPU_26),y)
  PROCESSOR		 = armo
  TEXTADDR		 = 0x02080000
  ZTEXTADDR		 = 0x01800000
  ZRELADDR		 = 0x02080000
  ifeq ($(CONFIG_GCC_NEW),y)
    CFLAGS_PROC		+= -mapcs-26 -mshort-load-bytes
    ifeq ($(CONFIG_CPU_ARM2),y)
      CFLAGS_PROC	+= -mcpu=arm2
      ASFLAGS_PROC	+= -m2
    endif
    ifeq ($(CONFIG_CPU_ARM3),y)
      CFLAGS_PROC	+= -mcpu=arm3
      ASFLAGS_PROC	+= -m3
    endif
  else
    ifeq ($(CONFIG_CPU_ARM2),y)
      CFLAGS_PROC	+= -m2
      ASFLAGS_PROC	+= -m2
    endif
    ifeq ($(CONFIG_CPU_ARM3),y)
      CFLAGS_PROC	+= -m3
      ASFLAGS_PROC	+= -m3
    endif
  endif
endif

ifeq ($(CONFIG_CPU_32),y)
  PROCESSOR		 = armv
  TEXTADDR		 = 0xC0008000
  ifeq ($(CONFIG_GCC_NEW),y)
    CFLAGS_PROC		+= -mapcs-32 -mshort-load-bytes
    ifeq ($(CONFIG_CPU_ARM6),y)
      CFLAGS_PROC	+= -mcpu=arm6
    endif
    ifeq ($(CONFIG_CPU_ARM7),y)
      CFLAGS_PROC	+= -mcpu=arm7
    endif
    ifeq ($(CONFIG_CPU_SA110),y)
      CFLAGS_PROC	+= -mcpu=strongarm110
    endif
  else
    CFLAGS_PROC		+= -m6
  endif
  ASFLAGS_PROC		+= -m6
endif

# Processor Architecture
# CFLAGS_PROC - processor dependent CFLAGS
# PROCESSOR   - processor type
# TEXTADDR    - Uncompressed kernel link text address
# ZTEXTADDR   - Compressed kernel link text address
# ZRELADDR    - Compressed kernel relocating address
#	        (point at which uncompressed kernel is loaded).
#

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
LINKFLAGS	 = -T $(TOPDIR)/arch/arm/vmlinux-$(PROCESSOR).lds -e stext -Ttext $(TEXTADDR)
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

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

symlinks::
	$(RM) include/asm-arm/arch include/asm-arm/proc
	(cd include/asm-arm; ln -sf arch-$(ARCHDIR) arch; ln -sf proc-$(PROCESSOR) proc)

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

