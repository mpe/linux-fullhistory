#
# arch/i386/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

AS86    =as86 -0 -a
LD86    =ld86 -0

zImage: $(CONFIGURE) bootsect setup compressed/vmlinux tools/build
	tools/build bootsect setup compressed/vmlinux $(ROOT_DEV) > zImage
	sync

compressed/vmlinux: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed vmlinux

zdisk: zImage
	dd bs=8192 if=zImage of=/dev/fd0

zlilo: $(CONFIGURE) zImage
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat zImage > $(INSTALL_PATH)/vmlinuz
	cp $(TOPDIR)/System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install: $(CONFIGURE) zImage
	sh ./install.sh $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) zImage $(TOPDIR)/System.map "$(INSTALL_PATH)"

tools/build: tools/build.c
	$(HOSTCC) $(CFLAGS) -o $@ $< -I$(TOPDIR)/include

setup: setup.o
	$(LD86) -s -o $@ $<

setup.o: setup.s
	$(AS86) -o $@ $<

setup.s: setup.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

bootsect: bootsect.o
	$(LD86) -s -o $@ $<

bootsect.o: bootsect.s
	$(AS86) -o $@ $<

bootsect.s: bootsect.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

dep:

clean:
	rm -f bootsect setup
	rm -f zImage tools/build
	@$(MAKE) -C compressed clean
