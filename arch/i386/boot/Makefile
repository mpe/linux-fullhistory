#
# arch/i386/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

AS86	=$(CROSS_COMPILE)as86 -0 -a
LD86	=$(CROSS_COMPILE)ld86 -0

BOOT_INCL =	$(TOPDIR)/include/linux/config.h \
		$(TOPDIR)/include/linux/autoconf.h \
		$(TOPDIR)/include/asm/boot.h

zImage: $(CONFIGURE) bootsect setup compressed/vmlinux tools/build
	$(OBJCOPY) compressed/vmlinux compressed/vmlinux.out
	tools/build bootsect setup compressed/vmlinux.out $(ROOT_DEV) > zImage

bzImage: $(CONFIGURE) bbootsect bsetup compressed/bvmlinux tools/build
	$(OBJCOPY) compressed/bvmlinux compressed/bvmlinux.out
	tools/build -b bbootsect bsetup compressed/bvmlinux.out $(ROOT_DEV) > bzImage

compressed/vmlinux: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed vmlinux

compressed/bvmlinux: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed bvmlinux

zdisk: $(BOOTIMAGE)
	dd bs=8192 if=$(BOOTIMAGE) of=/dev/fd0

zlilo: $(CONFIGURE) $(BOOTIMAGE)
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat $(BOOTIMAGE) > $(INSTALL_PATH)/vmlinuz
	cp $(TOPDIR)/System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install: $(CONFIGURE) $(BOOTIMAGE)
	sh -x ./install.sh $(KERNELRELEASE) $(BOOTIMAGE) $(TOPDIR)/System.map "$(INSTALL_PATH)"

tools/build: tools/build.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< -I$(TOPDIR)/include

setup: setup.o
	$(LD86) -s -o $@ $<

setup.o: setup.s
	$(AS86) -o $@ $<

setup.s: setup.S video.S Makefile $(BOOT_INCL) $(TOPDIR)/include/linux/version.h
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

bsetup: bsetup.o
	$(LD86) -s -o $@ $<

bsetup.o: bsetup.s
	$(AS86) -o $@ $<

bsetup.s: setup.S video.S Makefile $(BOOT_INCL) $(TOPDIR)/include/linux/version.h
	$(CPP) -D__BIG_KERNEL__ -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

bootsect: bootsect.o
	$(LD86) -s -o $@ $<

bootsect.o: bootsect.s
	$(AS86) -o $@ $<

bootsect.s: bootsect.S Makefile $(BOOT_INCL)
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

bbootsect: bbootsect.o
	$(LD86) -s -o $@ $<

bbootsect.o: bbootsect.s
	$(AS86) -o $@ $<

bbootsect.s: bootsect.S Makefile $(BOOT_INCL)
	$(CPP) -D__BIG_KERNEL__ -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

dep:

clean:
	rm -f tools/build
	rm -f setup bootsect zImage compressed/vmlinux.out
	rm -f bsetup bbootsect bzImage compressed/bvmlinux.out
	@$(MAKE) -C compressed clean
