#
# arch/sh/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

OBJS  = 

#
# Drop some uninteresting sections in the kernel.
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

# Don't build dependencies, this may die if $(CC) isn't gcc
dep:

clean:
	rm -f zImage zImage.tmp mkboot

dummy:

include $(TOPDIR)/Rules.make
