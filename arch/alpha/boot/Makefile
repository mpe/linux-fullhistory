#
# arch/alpha/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
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

all:	tools/lxboot tools/bootlx vmlinux
	@echo run mkfloppy on machine with floppy drive

msb:	tools/lxboot tools/bootlx vmlinux
	( cat tools/lxboot tools/bootlx vmlinux ) > /dev/rz0a
	disklabel -rw rz0 'linux' tools/lxboot tools/bootlx

vmlinux: tools/build $(TOPDIR)/vmlinux
	tools/build -v $(TOPDIR)/vmlinux > vmlinux

tools/lxboot: tools/build
	tools/build > tools/lxboot

tools/bootlx: bootloader tools/build
	tools/build -vb bootloader > tools/bootlx

tools/build: tools/build.c
	$(HOSTCC) tools/build.c -o tools/build

bootloader: $(OBJECTS)
	$(LD) -non_shared -T 0x20000000 -N \
		$(OBJECTS) \
		$(LIBS) \
		-o bootloader || \
		(rm -f bootloader && exit 1)

clean:
	rm -f vmlinux bootloader tools/build tools/bootlx tools/lxboot

dep:
