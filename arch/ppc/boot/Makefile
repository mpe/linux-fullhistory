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

linux.boot: $(TOPDIR)/vmlinux
	mkboot $(TOPDIR)/vmlinux $@ 0

clean:
	rm -f linux.boot 

dep:

