#
# i386/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

AS86    =as86 -0 -a
LD86    =ld86 -0
AS      =as
LD      =ld
HOSTCC  =gcc -I$(TOPDIR)/include
CC      =gcc -D__KERNEL__ -I$(TOPDIR)/include
MAKE    =make
CPP     =$(CC) -E
AR      =ar
STRIP   =strip

ifdef CONFIG_M486
CFLAGS := $(CFLAGS) -m486
else
ifdef CONFIG_M586
CFLAGS := $(CFLAGS) -mpentium
else
CFLAGS := $(CFLAGS) -m386
endif
endif

zBoot/zSystem: zBoot/*.c zBoot/*.S tools/zSystem
	$(MAKE) -C zBoot

zImage: $(CONFIGURE) boot/bootsect boot/setup zBoot/zSystem tools/build
	tools/build boot/bootsect boot/setup zBoot/zSystem $(ROOT_DEV) > zImage
	sync

zdisk: zImage
	dd bs=8192 if=zImage of=/dev/fd0

zlilo: $(CONFIGURE) zImage
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/zSystem.map ]; then mv $(INSTALL_PATH)/zSystem.map $(INSTALL_PATH)/zSystem.old; fi
	cat zImage > $(INSTALL_PATH)/vmlinuz
	cp zSystem.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

#
# Set these to indicate how to link it..
#
# -zmagic:
#
#LOWLDFLAGS     = -Ttext 0x1000
#HIGHLDFLAGS    = -Ttext 0x100000
#
# -qmagic (we need to remove the 32 byte header for bootup purposes)
#
LOWLDFLAGS      =-qmagic -Ttext 0xfe0
HIGHLDFLAGS     =-qmagic -Ttext 0xfffe0

tools/system:	boot/head.o init/main.o tools/version.o linuxsubdirs
	$(LD) $(LOWLDFLAGS) boot/head.o init/main.o tools/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) \
		-o tools/system
		nm tools/zSystem | grep -v '\(compiled\)\|\(\.o$$\)\|\( a \)' | \
		sort > System.map

boot/setup: boot/setup.o
	$(LD86) -s -o $@ $<

boot/setup.o: boot/setup.s
	$(AS86) -o $@ $<

boot/setup.s: boot/setup.S $(CONFIGURE) include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

boot/bootsect: boot/bootsect.o
	$(LD86) -s -o $@ $<

boot/bootsect.o: boot/bootsect.s
	$(AS86) -o $@ $<

boot/bootsect.s: boot/bootsect.S $(CONFIGURE) include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

tools/zSystem:	boot/head.o init/main.o tools/version.o linuxsubdirs
	$(LD) $(HIGHLDFLAGS) boot/head.o init/main.o tools/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) \
		-o tools/zSystem
	nm tools/zSystem | grep -v '\(compiled\)\|\(\.o$$\)\|\( a \)' | \
		sort > zSystem.map

#
# Leave these dummy entries for now to tell people that they are going away..
#
lilo:
	@echo
	@echo Uncompressed kernel images no longer supported. Use
	@echo \"make zlilo\" instead.
	@echo
	@exit 1

archclean:
	rm -f boot/bootsect boot/setup
