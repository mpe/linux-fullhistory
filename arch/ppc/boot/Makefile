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
	$(CC) $(CFLAGS) -c -o $*.o $<
.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<


ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00800000
GZIP_FLAGS = -v9

SYSTEM = $(TOPDIR)/vmlinux
OBJECTS := head.o inflate.o unzip.o misc.o vreset.o kbd.o
CFLAGS = -O2 -DSTDC_HEADERS -I$(TOPDIR)/include
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJCOPY_ARGS = -O elf32-powerpc


all:	$(TOPDIR)/zImage

mkprep : mkprep.c
	$(HOSTCC) $(CFLAGSINC) -o mkprep mkprep.c

piggyback : piggyback.c
	$(HOSTCC) $(CFLAGSINC) -o piggyback piggyback.c

find_name : find_name.c
	$(HOSTCC) $(CFLAGSINC) -o find_name find_name.c

floppy: $(TOPDIR)/vmlinux zImage 
	dd if=$(TOPDIR)/zImage of=/dev/fd0H1440 bs=64b

floppy.initrd: $(TOPDIR)/vmlinux zImage 
	dd if=$(TOPDIR)/zImage.initrd of=/dev/fd0H1440 bs=64b

znetboot : zImage mkprep
	cp $(TOPDIR)/zImage /usr/local/tftpboot/vmlinux

znetboot.initrd : zImage.initrd mkprep
	cp $(TOPDIR)/zImage.initrd /usr/local/tftpboot/vmlinux

#
# This really needs to go away.  Perhaps a 
# zImage.prep and zImage.chrp might be better.
# Once we're able to get a lilo-ish program 
# on prep systems this won't be a problem.
#   -- Cort
#
ifdef CONFIG_CHRP
zImage: zvmlinux
	cp zvmlinux $(TOPDIR)/zImage

zImage.initrd: zvmlinux.initrd
	cp zvmlinux.initrd $(TOPDIR)/zImage.initrd

zvmlinux: $(OBJECTS) $(SYSTEM) find_name vmlinux.gz piggyback
	./piggyback < vmlinux.gz | $(AS) -o piggy.o
	$(LD) $(ZLINKFLAGS) -o $@ $(OBJECTS) piggy.o
	rm -f piggy.o
else
zImage: zvmlinux mkprep
	mkprep -pbp zvmlinux $(TOPDIR)/zImage

zImage.initrd: zvmlinux.initrd mkprep
	mkprep -pbp zvmlinux.initrd $(TOPDIR)/zImage.initrd

zvmlinux: $(OBJECTS) $(SYSTEM) mkprep  find_name vmlinux.gz
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=image=vmlinux.gz \
		zvmlinux.tmp $@
	rm zvmlinux.tmp
endif

vmlinux.gz: $(TOPDIR)/vmlinux
	dd bs=64k skip=1 if=$(TOPDIR)/vmlinux | gzip -vf9 - > vmlinux.gz

zvmlinux.initrd: zvmlinux
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=initrd=ramdisk.image.gz \
		zvmlinux $@

clean:
	rm -f vmlinux* znetboot* zImage* zvmlinux* mkprep find_name
	rm -f $(TOPDIR)/{zImage*,znetboot*,zvmlinux*,vmlinux*}

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

