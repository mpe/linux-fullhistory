#
# arch/mips/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Ralf Baechle
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

OBJS  = milo.o a.out.o

#
# Drop some uninteresting sections in the kernel.
# This is only relevant for ELF kernels but doesn't hurt a.out
#
drop-sections	= .reginfo .mdebug
strip-flags	= $(addprefix --remove-section=,$(drop-sections))

#
# Fake compressed boot
#
zImage:	$(CONFIGURE) mkboot $(TOPDIR)/vmlinux
	$(OBJCOPY) $(strip-flags) $(TOPDIR)/vmlinux zImage.tmp
	./mkboot zImage.tmp zImage
	rm -f zImage.tmp

mkboot: mkboot.c
	$(HOSTCC) -o $@ $^

zdisk:	zImage
	if [ -f /etc/remote-mcopy ]; then \
		ssh rio mcopy -o - a:vmlinux <zImage; \
	else \
		mcopy -o zImage a:vmlinux; \
	fi

# Don't build dependencies, this may die if $(CC) isn't gcc
dep:

clean:
	rm -f zImage zImage.tmp mkboot

dummy:

include $(TOPDIR)/Rules.make
