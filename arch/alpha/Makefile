#
# alpha/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

AS      =as
LD      =ld
HOSTCC  =gcc -I$(TOPDIR)/include
CC      =gcc -D__KERNEL__ -I$(TOPDIR)/include
MAKE    =make
CPP     =$(CC) -E
AR      =ar
STRIP   =strip

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

tools/system:	boot/head.o init/main.o tools/version.o linuxsubdirs
	$(LD) $(LOWLDFLAGS) boot/head.o init/main.o tools/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) \
		-o tools/system
		nm tools/zSystem | grep -v '\(compiled\)\|\(\.o$$\)\|\( a \)' | \
		sort > System.map

boot/setup.o: boot/setup.s
	$(AS) -o $@ $<

boot/setup.s: boot/setup.S $(CONFIGURE) include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

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
