#
# arch/mips/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by  Waldorf Electronics,
# written by Ralf Baechle
#

#
# Fake compressed boot
#
zImage:	$(CONFIGURE) $(TOPDIR)/vmlinux
	ln -fs $(TOPDIR)/vmlinux zImage

zdisk:	zImage
	mcopy -n $(TOPDIR)/vmlinux a:vmlinux

dep:

clean:
	rm -f zImage
