#
# Makefile for the Sun Boot PROM interface library under
# Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = bootstr.o devmap.o devops.o init.o memory.o misc.o mp.o \
        palloc.o ranges.o segment.o tree.o console.o printf.o

all: promlib.a

promlib.a: $(OBJS)
	$(AR) rcs promlib.a $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
