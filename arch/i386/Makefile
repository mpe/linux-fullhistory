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
# 19990713  Artur Skawina <skawina@geocities.com>
#           Added '-march' and '-mpreferred-stack-boundary' support
#

LD=$(CROSS_COMPILE)ld -m elf_i386
OBJCOPY=$(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
LDFLAGS=-e stext
LINKFLAGS =-T $(TOPDIR)/arch/i386/vmlinux.lds $(LDFLAGS)

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) $(CFLAGS_PIPE) $(CFLAGS_NSR)

# prevent gcc from keeping the stack 16 byte aligned
CFLAGS += $(shell if $(CC) -mpreferred-stack-boundary=2 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mpreferred-stack-boundary=2"; fi)

ifdef CONFIG_M386
CFLAGS := $(CFLAGS) -m386 -DCPU=386
AFLAGS := $(AFLAGS) -DCPU=386
endif

ifdef CONFIG_M486
CFLAGS := $(CFLAGS) -DCPU=486
CFLAGS += $(shell if $(CC) -march=i486 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=i486"; else echo "-m486"; fi)
AFLAGS := $(AFLAGS) -DCPU=486
endif

ifdef CONFIG_M586
CFLAGS := $(CFLAGS) -DCPU=586
CFLAGS += $(shell if $(CC) -march=i586 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=i586"; fi)
AFLAGS := $(AFLAGS) -DCPU=586
endif

ifdef CONFIG_M586TSC
CFLAGS := $(CFLAGS) -DCPU=586
CFLAGS += $(shell if $(CC) -march=i586 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=i586"; fi)
AFLAGS := $(AFLAGS) -DCPU=586
endif

ifdef CONFIG_M686
CFLAGS := $(CFLAGS) -DCPU=686
CFLAGS += $(shell if $(CC) -march=i686 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=i686"; fi)
AFLAGS := $(AFLAGS) -DCPU=686
endif

ifdef CONFIG_MK7
CFLAGS := $(CFLAGS) -DCPU=686 -march=pentiumpro -mpentiumpro -malign-functions=4 -fschedule-insns2 -mwide-multiply -fexpensive-optimizations
CFLAGS += $(shell if $(CC) -march=i686 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=i686"; fi)
AFLAGS := $(AFLAGS) -DCPU=686
endif

HEAD := arch/i386/kernel/head.o arch/i386/kernel/init_task.o

SUBDIRS := $(SUBDIRS) arch/i386/kernel arch/i386/mm arch/i386/lib
CORE_FILES := arch/i386/kernel/kernel.o arch/i386/mm/mm.o $(CORE_FILES)
LIBS := $(TOPDIR)/arch/i386/lib/lib.a $(LIBS) $(TOPDIR)/arch/i386/lib/lib.a

ifdef CONFIG_MATH_EMULATION
SUBDIRS := $(SUBDIRS) arch/i386/math-emu
DRIVERS := $(DRIVERS) arch/i386/math-emu/math.a
endif

arch/i386/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/i386/kernel

arch/i386/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/i386/mm

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: arch/i386/vmlinux.lds

arch/i386/vmlinux.lds: arch/i386/vmlinux.lds.S FORCE
	$(CPP) -C -P -imacros $(HPATH)/asm-i386/page_offset.h -Ui386 arch/i386/vmlinux.lds.S >arch/i386/vmlinux.lds

FORCE: ;

.PHONY: zImage bzImage compressed zlilo bzlilo zdisk bzdisk install \
		clean archclean archmrproper archdep

zImage: vmlinux
	@$(MAKEBOOT) zImage

bzImage: vmlinux
	@$(MAKEBOOT) bzImage

compressed: zImage

zlilo: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=zImage zlilo

bzlilo: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zlilo

zdisk: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=zImage zdisk

bzdisk: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zdisk

install: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage install

archclean:
	@$(MAKEBOOT) clean

archmrproper:
	rm -f arch/i386/vmlinux.lds

archdep:
	@$(MAKEBOOT) dep
