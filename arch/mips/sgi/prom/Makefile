# $Id: Makefile,v 1.1.1.1 1997/06/01 03:16:40 ralf Exp $
# Makefile for the SGI arcs prom monitor library routines
# under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

OBJS  = console.o init.o printf.o memory.o tree.o tags.o env.o \
      cmdline.o misc.o time.o file.o

all: promlib.a

promlib.a: $(OBJS)
	$(AR) rcs promlib.a $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

include $(TOPDIR)/Rules.make
