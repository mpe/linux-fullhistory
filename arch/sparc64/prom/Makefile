# $Id: Makefile,v 1.5 1999/12/21 04:02:26 davem Exp $
# Makefile for the Sun Boot PROM interface library under
# Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

OBJS  = bootstr.o devops.o init.o memory.o misc.o \
        tree.o console.o printf.o p1275.o map.o

all: promlib.a

promlib.a: $(OBJS)
	$(AR) rcs promlib.a $(OBJS)
	sync

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $< -o $*.o

dep:
	$(CPP) $(CPPFLAGS) -M *.c > .depend

include $(TOPDIR)/Rules.make
