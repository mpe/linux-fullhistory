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
DROP_SECTIONS	= .reginfo .mdebug

#
# The new options of binutils 2.6 help to shrink object files a lot.
# This is especially useful for booting from floppy.  Though we
# don't yet require everyone to have binutils 2.6 installed.
#
OBJDUMP_VERSION =  $(word 4,$(shell $(OBJDUMP) --version))
ifneq ($(OBJDUMP_VERSION),2.5.2)
KEEP = kernel_entry boot_info screen_info _end
STRIP_FLAGS = $(addprefix --keep-symbol=,$(KEEP))
else
STRIP_FLAGS = --discard-all
endif

#
# Fake compressed boot
#
ifdef CONFIG_ELF_KERNEL
zImage:	$(CONFIGURE) $(TOPDIR)/vmlinux
	cp $(TOPDIR)/vmlinux zImage.tmp
	$(STRIP) $(addprefix --remove-section=,$(DROP_SECTIONS)) \
		--strip-symbol=blurb zImage.tmp
	$(LD) -oformat=$(oformat) -N -e except_vec0 -Ttext=0x80000000 \
		-o zImage zImage.tmp
	rm -f zImage.tmp
	$(STRIP) $(STRIP_FLAGS) zImage
else
zImage:	$(CONFIGURE) $(TOPDIR)/vmlinux
	cp $(TOPDIR)/vmlinux $@
	$(STRIP) $(STRIP_FLAGS) $@
endif

zdisk:	zImage
	mcopy -n zImage a:vmlinux

dep:
	$(CPP) -M *.[cS] > .depend

clean:
	rm -f zImage zImage.tmp

dummy:

include $(TOPDIR)/Rules.make
