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


ZLINKFLAGS = -T ../ld.script -Ttext 0x00800000
GZIP_FLAGS = -v9

SYSTEM = $(TOPDIR)/vmlinux

OBJECTS = head.o inflate.o unzip.o misc.o vreset.o

CFLAGS = -O2 -DSTDC_HEADERS -I$(TOPDIR)/include

all:	$(TOPDIR)/zImage

mkprep : mkprep.c
	$(HOSTCC) $(CFLAGSINC) -o mkprep mkprep.c

find_name : find_name.c
	$(HOSTCC) $(CFLAGSINC) -o find_name find_name.c

mk_type41: mk_type41.c
	$(HOSTCC) $(CFLAGSINC) -o mk_type41 mk_type41.c

piggyback: piggyback.c
	$(HOSTCC) $(CFLAGS) -o piggyback piggyback.c

floppy: $(TOPDIR)/vmlinux zImage 
	dd if=$(TOPDIR)/zImage of=/dev/fd0H1440 bs=64b

netboot : $(TOPDIR)/vmlinux mkprep
	mkprep $(TOPDIR)/vmlinux $(TOPDIR)/netboot

znetboot : zvmlinux mkprep
	mkprep zvmlinux $(TOPDIR)/znetboot
	cp $(TOPDIR)/znetboot /usr/local/tftpboot/vmlinux

rcpboot : znetboot
	rcp $(TOPDIR)/znetboot charon:/usr/tftpboot/vmlinux

zImage: zvmlinux mkprep
	mkprep -pbp zvmlinux $(TOPDIR)/zImage

install: zImage
	dd if=$(TOPDIR)/zImage of=/dev/sda4
	ln -s /dev/sda4 $(INSTALL_PATH)/vmlinuz
	cp $(TOPDIR)/System.map $(INSTALL_PATH)/	

zvmlinux: $(OBJECTS) $(SYSTEM) mkprep  find_name
	mkprep $(TOPDIR)/vmlinux -|gzip ${GZIP_FLAGS}|mkprep -asm - -|$(AS) -o piggy.o
	$(LD) $(ZLINKFLAGS) -o zvmlinux $(OBJECTS) piggy.o
	rm -f piggy.o

clean:
	rm -f piggyback zvmlinux mk_type41 mkprep mkboot find_name
	rm -f $(TOPDIR)/{zImage,znetboot,netboot}

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

