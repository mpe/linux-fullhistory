#
# alpha/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

AS      =as
LD      =ld
HOSTCC  =gcc -I$(TOPDIR)/include
CC      =gcc -D__KERNEL__ -I$(TOPDIR)/include
MAKE    =make
CPP     =$(CC) -E
AR      =ar
STRIP   =strip

arch-all: tools/system

tools/system:	boot/head.o init/main.o tools/version.o linuxsubdirs
	$(LD) $(LOWLDFLAGS) boot/head.o init/main.o tools/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) \
		-o tools/system
		nm tools/zSystem | grep -v '\(compiled\)\|\(\.o$$\)\|\( a \)' | \
		sort > System.map


#
# Leave these dummy entries for now to tell people that they are going away..
#
lilo:
	@echo
	@echo Uncompressed kernel images no longer supported. Use
	@echo \"make zlilo\" instead.
	@echo
	@exit 1

archclean:
	@rm -f boot/head.o
