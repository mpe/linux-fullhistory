#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#
.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS)  -DINITRD_OFFSET=$(IOFF) -DINITRD_SIZE=$(ISZ) -DZIMAGE_OFFSET=$(ZOFF) -DZIMAGE_SIZE=$(ZSZ) -DKERNELBASE=$(KERNELBASE) -c -o $*.o $<
.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<


ZOFF = 0
ZSZ = 0
IOFF = 0
ISZ = 0
#ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00800000
ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00600000
GZIP_FLAGS = -v9

OBJECTS := head.o misc.o vreset.o kbd.o ../coffboot/zlib.o # inflate.o unzip.o
#OBJECTS := crt0.o start.o vreset.o
CFLAGS = -O2 -DSTDC_HEADERS -fno-builtin -I$(TOPDIR)/include
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJCOPY_ARGS = -O elf32-powerpc

all:	zImage


ifeq ($(CONFIG_PREP),y)
mkprep : mkprep.c
	$(HOSTCC) -DKERNELBASE=$(KERNELBASE) -o mkprep mkprep.c

floppy: $(TOPDIR)/vmlinux zImage
	dd if=zImage of=/dev/fd0H1440 bs=64b

znetboot : zImage
	cp zImage /tftpboot/zImage.prep

znetboot.initrd : zImage.initrd
	cp zImage.initrd /tftpboot/zImage.prep

zvmlinux: $(OBJECTS) ../coffboot/vmlinux.gz
#
# build the boot loader image and then compute the offset into it
# for the kernel image
#
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
#
# then with the offset rebuild the bootloader so we know where the kernel is
#
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 \
		-DZIMAGE_OFFSET=`./offset zvmlinux image` \
		-DZIMAGE_SIZE=`./size zvmlinux image` -DKERNELBASE=$(KERNELBASE) \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
	rm zvmlinux.tmp

zvmlinux.initrd: zvmlinux
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp zvmlinux.initrd
	$(CC) $(CFLAGS) -DINITRD_OFFSET=`./offset zvmlinux.initrd initrd` \
		-DINITRD_SIZE=`./size zvmlinux.initrd initrd` \
		-DZIMAGE_OFFSET=`./offset zvmlinux.initrd image` \
		-DZIMAGE_SIZE=`./size zvmlinux.initrd image` \
		-DKERNELBASE=$(KERNELBASE) -c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp $@
	rm zvmlinux.initrd.tmp

zImage: zvmlinux mkprep
	./mkprep -pbp zvmlinux zImage

zImage.initrd: zvmlinux.initrd mkprep
	./mkprep -pbp zvmlinux.initrd zImage.initrd
else
mkprep:

floppy:

znetboot:

znetboot.initrd:

zvmlinux:

zvmlinux.initrd:

zImage:

zImage.initrd:
endif



# just here to match coffboot/Makefile
vmlinux.coff:

vmlinux.coff.initrd:

clean:
	rm -f vmlinux* zvmlinux* mkprep zImage*

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

