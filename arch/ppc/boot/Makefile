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

OBJECTS = head.o main.o

all:	linux.boot

#zImage: $(CONFIGURE) bootsect setup compressed/vmlinux tools/build
zImage: compressed/vmlinux mk_type41
	mkboot compressed/vmlinux vmlinux 0
	mk_type41 vmlinux zImage
	cp compressed/vmlinux zBoot
	rm -f vmlinux compressed/vmlinux

zdisk: zImage
	dd if=zImage of=/dev/fd0 bs=36b	

compressed/vmlinux: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed vmlinux

tImage: mk_type41 $(TOPDIR)/vmlinux
	mkboot $(TOPDIR)/vmlinux vmlinux 0
	mk_type41 vmlinux tImage
	rm -f vmlinux

bImage: mk_type41 $(TOPDIR)/vmlinux.no_ramdisk
	mkboot $(TOPDIR)/vmlinux.no_ramdisk vmlinux 0
	mk_type41 vmlinux bImage
	rm -f vmlinux

xImage: compressed/vmlinux.no_ramdisk
	mkboot compressed/vmlinux.no_ramdisk vmlinux 0
	mk_type41 vmlinux xImage
	rm -f vmlinux compressed/vmlinux.no_ramdisk

compressed/vmlinux.no_ramdisk: $(TOPDIR)/vmlinux.no_ramdisk
	@$(MAKE) -C compressed vmlinux.no_ramdisk

bdisk: xImage
	dd if=xImage of=/dev/fd0 bs=36b
	
linux.boot: $(TOPDIR)/vmlinux
	mkboot $(TOPDIR)/vmlinux $@ 0

mk_type41: mk_type41.c
	cc -o mk_type41 mk_type41.c
	
clean:
	rm -f linux.boot 

dep:
fastdep:
