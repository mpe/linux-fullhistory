#
# arch/alpha/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

# enable this for linking under OSF/1:
LINKFLAGS = -non_shared -T 0x20000000 -N
# enable this for linking under Linux:
#LINKFLAGS = -static -T bootloader.lds -N 

.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

OBJECTS = head.o main.o
TARGETS = tools/lxboot tools/bootlx vmlinux vmlinux.gz

all:	$(TARGETS)
	@echo Ready to install kernel in $(shell pwd)/vmlinux.gz

msb:	tools/lxboot tools/bootlx vmlinux
	( cat tools/lxboot tools/bootlx vmlinux ) > /dev/rz0a
	disklabel -rw rz0 'linux' tools/lxboot tools/bootlx

vmlinux.gz: vmlinux
	gzip -fv vmlinux

vmlinux: tools/build $(TOPDIR)/vmlinux
	tools/build -v $(TOPDIR)/vmlinux > vmlinux
#	cp $(TOPDIR)/vmlinux vmlinux
#	quickstrip vmlinux

tools/lxboot: tools/build
	tools/build > tools/lxboot

tools/bootlx: bootloader tools/build
	tools/build -vb bootloader > tools/bootlx

tools/build: tools/build.c
	$(HOSTCC) tools/build.c -o tools/build

bootloader: $(OBJECTS)
	$(LD) $(LINKFLAGS) \
		$(OBJECTS) \
		$(LIBS) \
		-o bootloader || \
		(rm -f bootloader && exit 1)

clean:
	rm -f $(TARGETS) bootloader tools/build

dep:
