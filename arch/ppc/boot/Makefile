#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
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
GZIP_FLAGS = -9

SYSTEM = $(TOPDIR)/vmlinux

OBJECTS = head.o inflate.o unzip.o misc.o vreset.o

CFLAGS = -O2 -DSTDC_HEADERS -I$(TOPDIR)/include

all:	$(TOPDIR)/zImage

mkboot : cortstrip.c
	$(HOSTCC) $(CFLAGSINC) -Wl,-static -o mkboot cortstrip.c

mk_type41: mk_type41.c
	$(HOSTCC) $(CFLAGSINC) -Wl,-static -o mk_type41 mk_type41.c

floppy: zImage $(TOPDIR)/vmlinux
	dd if=$(TOPDIR)/zImage of=/dev/fd0H1440 bs=64b

netboot : $(TOPDIR)/vmlinux mkboot
	mkboot $(TOPDIR)/vmlinux $(TOPDIR)/netboot
#	rcp $(TOPDIR)/netboot charon:/usr/tftpboot/vmlinux

znetboot : mkboot zvmlinux
	mkboot zvmlinux $(TOPDIR)/znetboot
	rcp $(TOPDIR)/znetboot charon:/usr/tftpboot/vmlinux

zImage: mk_type41 zvmlinux
# make znetboot ourselves since using the normal dep
# will rcp it -- Cort
	mkboot zvmlinux $(TOPDIR)/znetboot
	mk_type41 $(TOPDIR)/znetboot $(TOPDIR)/zImage

install: zImage
	dd if=$(TOPDIR)/zImage of=/dev/sda4
	ln -s /dev/sda4 $(INSTALL_PATH)/vmlinuz
	cp $(TOPDIR)/System.map $(INSTALL_PATH)/	

zvmlinux: $(OBJECTS) $(SYSTEM) piggyback netboot $(TOPDIR)/vmlinux
	gzip ${GZIP_FLAGS} <$(TOPDIR)/netboot | ./piggyback | $(AS) -o piggy.o
	$(LD) $(ZLINKFLAGS) -o zvmlinux $(OBJECTS) piggy.o
	rm -f piggy.o xx_boot

head.o:	head.s

head.s: head.S $(TOPDIR)/include/linux/tasks.h
	$(CPP) -traditional head.S -o head.s

piggyback: piggyback.c
	$(HOSTCC) $(CFLAGS) -o piggyback piggyback.c

clean:
	rm -f piggyback zvmlinux mk_type41 mkprep mkboot
	rm -f $(TOPDIR)/{zImage,znetboot,netboot}

dep:
