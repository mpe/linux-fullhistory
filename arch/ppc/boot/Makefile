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

all:	linux.boot mkboot

linux.boot: $(TOPDIR)/vmlinux mkboot
	rsh charon /bin/rm -f /usr/tftpboot/vmlinux
	/bin/rm -f /usr/tftpboot/vmlinux
	./mkboot $(TOPDIR)/vmlinux /usr/tftpboot/vmlinux
	rcp /usr/tftpboot/vmlinux charon:/usr/tftpboot/vmlinux
	sync

tImage: mk_type41 $(TOPDIR)/vmlinux mkboot
	./mkboot $(TOPDIR)/vmlinux vmlinux 
	mk_type41 vmlinux tImage
	rm vmlinux

mkboot : cortstrip.c
	$(HOSTCC) -o mkboot cortstrip.c

mk_type41: mk_type41.c
	gcc -o mk_type41 mk_type41.c

clean:
	rsh charon /bin/rm -f /usr/tftpboot/vmlinux
	/bin/rm -f /usr/tftpboot/vmlinux

dep:
