# Makefile for the Sparc low level /boot module.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)

OBJS	=bare.o init_me.o ../kernel/promops.o ../lib/lib.a
BOOTLINKFLAGS = -N -Ttext 0x200000 -e _first_adr_in_text

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<
.S.s:
	$(CC) -D__ASSEMBLY__ -D__KERNEL__ -ansi -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -D__KERNEL__ -ansi -c -o $*.o $<

all: boot

boot: $(OBJS)
	$(LD) $(BOOTLINKFLAGS) $(OBJS) -o boot

dep:
	$(CPP) -M *.c > .depend
	$(CPP) -M -D__ASSEMBLY__ -ansi *.S >>.depend

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
