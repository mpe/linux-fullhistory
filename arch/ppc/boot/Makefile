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

ifeq ($(CONFIG_ALL_PPC),y)
# yes, we want to build prep stuff
CONFIG_PREP = y
endif

ifeq ($(CONFIG_MBX),y)
ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00100000
else
ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00600000
endif

GZIP_FLAGS = -v9

OBJECTS := head.o misc.o ../coffboot/zlib.o
CFLAGS = -O2 -DSTDC_HEADERS -fno-builtin -I$(TOPDIR)/include
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJCOPY_ARGS = -O elf32-powerpc

ifeq ($(CONFIG_MBX),y)
OBJECTS += mbxtty.o
CFLAGS += -DCONFIG_MBX
else
OBJECTS += vreset.o kbd.o
 ifeq ($(CONFIG_SERIAL_CONSOLE),y)
  OBJECTS += ns16550.o
 endif
endif

all:	zImage

ifeq ($(CONFIG_PREP),y)
zvmlinux.initrd: zvmlinux
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp zvmlinux.initrd
	$(CC) $(CFLAGS) -DINITRD_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd initrd` \
		-DINITRD_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd initrd` \
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd image` \
		-DKERNELBASE=$(KERNELBASE) -c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp $@
	rm zvmlinux.initrd.tmp
endif
ifeq ($(CONFIG_MBX),y)
zvmlinux.initrd: zvmlinux
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp zvmlinux.initrd
	$(CC) $(CFLAGS) -DINITRD_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd initrd` \
		-DINITRD_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd initrd` \
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd image` \
		-DKERNELBASE=$(KERNELBASE) -c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp $@
	rm zvmlinux.initrd.tmp
endif

ifeq ($(CONFIG_PREP),y)
zImage: zvmlinux mkprep
	./mkprep -pbp zvmlinux zImage
else
ifeq ($(CONFIG_MBX),y)
zImage: zvmlinux
	ln -sf zvmlinux zImage
else
zImage:
endif
endif

ifeq ($(CONFIG_PREP),y)
zImage.initrd: zvmlinux.initrd mkprep
	./mkprep -pbp zvmlinux.initrd zImage.initrd
endif
ifeq ($(CONFIG_MBX),y)
zImage.initrd: zvmlinux.initrd
	ln -sf zvmlinux.initrd zImage.initrd
endif

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
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux image` -DKERNELBASE=$(KERNELBASE) \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
	rm zvmlinux.tmp

floppy: $(TOPDIR)/vmlinux zImage
ifeq ($(CONFIG_PREP),y)
	dd if=zImage of=/dev/fd0H1440 bs=64b
endif

ifeq ($(CONFIG_PREP),y)
mkprep : mkprep.c
	$(HOSTCC) -DKERNELBASE=$(KERNELBASE) -o mkprep mkprep.c
endif

ifeq ($(CONFIG_PREP),y)
znetboot : zImage
	cp zImage /tftpboot/zImage.prep
else
ifeq ($(CONFIG_MBX),y)
znetboot : zImage
	cp zImage /tftpboot/zImage.mbx
else
znetboot :
endif
endif

znetboot.initrd : zImage.initrd
ifeq ($(CONFIG_PREP),y)
	cp zImage.initrd /tftpboot/zImage.prep
endif
ifeq ($(CONFIG_MBX),y)
	cp zImage.initrd /tftpboot/zImage.mbx
endif

clean:
	rm -f vmlinux* zvmlinux* mkprep zImage*

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

# just here to match coffboot/Makefile
vmlinux.coff:

vmlinux.coff.initrd:
