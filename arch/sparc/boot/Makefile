# $Id: Makefile,v 1.2 1995/11/25 00:57:37 davem Exp $
# Makefile for the Sparc low level /boot module.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)

OBJS	=bare.o init_me.o ../kernel/promops.o ../lib/lib.a
BOOTLINKFLAGS = -N -Ttext 0x200000 -e _first_adr_in_text

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
