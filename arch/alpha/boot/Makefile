#
# arch/alpha/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

ifdef CONFIG_CROSSCOMPILE
# enable this for linking under OSF/1:
LINKFLAGS = -non_shared -T 0x20000000 -N
else
# enable this for linking under Linux:
LINKFLAGS = -static -T bootloader.lds -N 
endif

.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

OBJECTS = head.o main.o
TARGETS = vmlinux.gz

all:	$(TARGETS)
	@echo Ready to install kernel in $(shell pwd)/vmlinux.gz

# normally no need to build these:
rawboot: vmlinux.nh tools/lxboot tools/bootlx vmlinux

msb:	tools/lxboot tools/bootlx vmlinux.nh
	( cat tools/lxboot tools/bootlx vmlinux.nh ) > /dev/rz0a
	disklabel -rw rz0 'linux' tools/lxboot tools/bootlx

vmlinux.gz: vmlinux
	gzip -fv vmlinux

#
# A raw binary without header.  Used by raw boot.
#
vmlinux.nh: tools/build
	tools/build -v $(TOPDIR)/vmlinux > vmlinux.nh

vmlinux: $(TOPDIR)/vmlinux
	cp $(TOPDIR)/vmlinux vmlinux
	quickstrip vmlinux

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
