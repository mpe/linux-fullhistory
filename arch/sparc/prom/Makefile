# $Id: Makefile,v 1.6 1998/01/30 10:58:59 jj Exp $
# Makefile for the Sun Boot PROM interface library under
# Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

OBJS  = bootstr.o devmap.o devops.o init.o memory.o misc.o mp.o \
	palloc.o ranges.o segment.o console.o printf.o tree.o

ifeq ($(CONFIG_SUN4),y)
OBJS += sun4prom.o
endif

all: promlib.a

promlib.a: $(OBJS)
	$(AR) rcs promlib.a $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

include $(TOPDIR)/Rules.make
