#
# ia64/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998 by David Mosberger-Tang <davidm@hpl.hp.com>
#

LINKFLAGS = -static -T bootloader.lds

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c -o $*.o $<

OBJECTS	= bootloader.o
TARGETS =

ifdef CONFIG_IA64_HP_SIM
 TARGETS += bootloader
endif

all:	$(TARGETS)

bootloader: $(OBJECTS)
	$(LD) $(LINKFLAGS) $(OBJECTS) $(LIBS) -o bootloader

clean:
	rm -f $(TARGETS)

dep:
