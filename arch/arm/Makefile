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
# Copyright (C) 1995, 1996 by Russell King

CFLAGS_PROC	:=
ASFLAGS_PROC	:=

ifeq ($(CONFIG_CPU_ARM2),y)
PROCESSOR	 = armo
ASFLAGS_PROC	+= -m2
ifeq ($(CONFIG_BINUTILS_NEW),y)
CFLAGS_PROC	+= -mcpu=arm2
ASFLAGS_PROC	+= -m2
else
CFLAGS_PROC	+= -m2
ASFLAGS_PROC	+= -m2
endif
endif

ifeq ($(CONFIG_CPU_ARM3),y)
PROCESSOR	 = armo
ifeq ($(CONFIG_BINUTILS_NEW),y)
CFLAGS_PROC	+= -mcpu=arm3
ASFLAGS_PROC	+= -m3
else
CFLAGS_PROC	+= -m3
ASFLAGS_PROC	+= -m3
endif
endif

ifeq ($(CONFIG_CPU_ARM6),y)
PROCESSOR	 = armv
ifeq ($(CONFIG_BINUTILS_NEW),y)
CFLAGS_PROC	+= -mcpu=arm6
ASFLAGS_PROC	+= -m6
else
CFLAGS_PROC	+= -m6
ASFLAGS_PROC	+= -m6
endif
endif

ifeq ($(CONFIG_CPU_SA110),y)
PROCESSOR	 = armv
ifeq ($(CONFIG_BINUTILS_NEW),y)
CFLAGS_PROC	+= -mcpu=strongarm110
ASFLAGS_PROC	+= -m6
else
CFLAGS_PROC	+= -m6
ASFLAGS_PROC	+= -m6
endif
endif

# Processor Architecture
# CFLAGS_PROC - processor dependent CFLAGS
# PROCESSOR - processor type
# TEXTADDR - Uncompressed kernel link text address
# ZTEXTADDR - Compressed kernel link text address
# ZRELADDR - Compressed kernel relocating address (point at which uncompressed kernel is loaded).
#

COMPRESSED_HEAD	 = head.o

ifeq ($(PROCESSOR),armo)
ifeq ($(CONFIG_BINUTILS_NEW),y)
CFLAGS_PROC	+= -mapcs-26 -mshort-load-bytes
endif
TEXTADDR	 = 0x02080000
ZTEXTADDR	 = 0x01800000
ZRELADDR	 = 0x02080000
endif

ifeq ($(CONFIG_ARCH_A5K),y)
MACHINE		 = a5k
ARCHDIR		 = arc
COMPRESSED_EXTRA = $(TOPDIR)/arch/arm/lib/ll_char_wr.o
endif

ifeq ($(CONFIG_ARCH_ARC),y)
MACHINE		 = arc
ARCHDIR		 = arc
COMPRESSED_EXTRA = $(TOPDIR)/arch/arm/lib/ll_char_wr.o
endif

ifeq ($(PROCESSOR),armv)
ifeq ($(CONFIG_BINUTILS_NEW),y)
CFLAGS_PROC	+= -mapcs-32 -mshort-load-bytes
endif
TEXTADDR	 = 0xC0008000
endif

ifeq ($(CONFIG_ARCH_RPC),y)
MACHINE		 = rpc
ARCHDIR		 = rpc
COMPRESSED_EXTRA = $(TOPDIR)/arch/arm/lib/ll_char_wr.o
ZTEXTADDR	 = 0x10008000
ZRELADDR	 = 0x10008000
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
MACHINE		 = ebsa110
ARCHDIR		 = ebsa110
ZTEXTADDR	 = 0x00008000
ZRELADDR	 = 0x00008000
endif

ifeq ($(CONFIG_ARCH_EBSA285),y)
MACHINE		 = ebsa285
ARCHDIR		 = ebsa285
ZTEXTADDR	 = 0x00008000
ZRELADDR	 = 0x00008000
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
MACHINE		 = nexuspci
TEXTADDR	 = 0xc0000000
ZTEXTADDR	 = 0x40200000
ZRELADDR	 = 0x40000000
COMPRESSED_EXTRA = $(TOPDIR)/arch/arm/lib/ll_char_wr_scc.o
COMPRESSED_HEAD	 = head-nexuspci.o
endif

OBJDUMP		 = $(CROSS_COMPILE)objdump
PERL		 = perl
LD		 = $(CROSS_COMPILE)ld -m elf_arm
CPP		 = $(CC) -E
OBJCOPY		 = $(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
ARCHCC		:= $(word 1,$(CC))
GCCLIB		:= `$(ARCHCC) $(CFLAGS_PROC) --print-libgcc-file-name`
GCCARCH		:= -B/usr/src/bin/arm/arm-linuxelf- 
HOSTCFLAGS	:= $(CFLAGS:-fomit-frame-pointer=)
ifeq ($(CONFIG_FRAME_POINTER),y)
CFLAGS		:= $(CFLAGS:-fomit-frame-pointer=)
endif
CFLAGS		:= $(CFLAGS_PROC) $(CFLAGS) -pipe
ASFLAGS		:= $(ASFLAGS_PROC) $(ASFLAGS)
LINKFLAGS	 = -T $(TOPDIR)/arch/arm/vmlinux.lds -e stext -Ttext $(TEXTADDR)
ZLINKFLAGS	 = -Ttext $(ZTEXTADDR)

SUBDIRS		:= $(SUBDIRS:drivers=) arch/arm/lib arch/arm/kernel arch/arm/mm arch/arm/drivers
HEAD		:= arch/arm/kernel/head-$(PROCESSOR).o arch/arm/kernel/init_task.o
CORE_FILES	:= arch/arm/kernel/kernel.o arch/arm/mm/mm.o $(CORE_FILES)
LIBS		:= arch/arm/lib/lib.a $(LIBS) $(GCCLIB)

BLOCK_DRIVERS	:= arch/arm/drivers/block/block.a
CDROM_DRIVERS	:= drivers/cdrom/cdrom.a
CHAR_DRIVERS	:= arch/arm/drivers/char/char.a
MISC_DRIVERS	:= drivers/misc/misc.a
NET_DRIVERS	:= drivers/net/net.a
PARIDE_DRIVERS	:= drivers/block/paride/paride.a
PCI_DRIVERS	:= drivers/pci/pci.a
SCSI_DRIVERS	:= drivers/scsi/scsi.a
SOUND_DRIVERS	:= drivers/sound/sound.a

ifeq ($(CONFIG_ARCH_ACORN),y)
BLOCK_DRIVERS	+= drivers/acorn/block/acorn-block.a
CHAR_DRIVERS	+= drivers/acorn/char/acorn-char.a
NET_DRIVERS	+= drivers/acorn/net/acorn-net.a drivers/net/net.a
SCSI_DRIVERS	+= drivers/acorn/scsi/acorn-scsi.a
endif

DRIVERS		:= $(BLOCK_DRIVERS) $(CHAR_DRIVERS) $(MISC_DRIVERS) $(NET_DRIVERS)

ifeq ($(CONFIG_SCSI),y)
DRIVERS		:= $(DRIVERS) $(SCSI_DRIVERS)
endif
ifneq ($(CONFIG_CD_NO_IDESCSI)$(CONFIG_BLK_DEV_IDECD)$(CONFIG_BLK_DEV_SR),)
DRIVERS		:= $(DRIVERS) $(CDROM_DRIVERS)
endif
ifdef CONFIG_PCI
DRIVERS		:= $(DRIVERS) $(PCI_DRIVERS)
endif
ifeq ($(CONFIG_SOUND),y)
DRIVERS		:= $(DRIVERS) $(SOUND_DRIVERS)
endif
ifeq ($(CONFIG_PARIDE),y)
DRIVERS		:= $(DRIVERS) $(PARIDE_DRIVERS)
endif

symlinks::
	$(RM) include/asm-arm/arch include/asm-arm/proc
	(cd include/asm-arm; ln -sf arch-$(ARCHDIR) arch; ln -sf proc-$(PROCESSOR) proc)

# Once we've finished integrating the sources, the @$(MAKE) will disappear
mrproper::
	rm -f include/asm-arm/arch include/asm-arm/proc
	@$(MAKE) -C arch/$(ARCH)/drivers mrproper

arch/arm/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/kernel

arch/arm/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/arm/mm

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

zImage: vmlinux
	@$(MAKEBOOT) zImage

zinstall: vmlinux
	@$(MAKEBOOT) zinstall

Image: vmlinux
	@$(MAKEBOOT) Image

install: vmlinux
	@$(MAKEBOOT) install

# My testing targets (that short circuit a few dependencies)
zImg:;	@$(MAKEBOOT) zImage
Img:;	@$(MAKEBOOT) Image
i:;	@$(MAKEBOOT) install
zi:;	@$(MAKEBOOT) zinstall

archclean:
	@$(MAKEBOOT) clean
	$(RM) arch/arm/lib/constants.h

archdep:
	@$(MAKEBOOT) dep
