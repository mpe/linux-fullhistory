# $Id: Makefile,v 1.1 1996/12/27 08:49:10 jj Exp $
# Makefile for the Sun Boot PROM interface library under
# Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

OBJS  = bootstr.o devops.o init.o memory.o misc.o mp.o \
        ranges.o tree.o console.o printf.o

all: promlib.a

promlib.a: $(OBJS)
	$(AR) rcs promlib.a $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

include $(TOPDIR)/Rules.make
