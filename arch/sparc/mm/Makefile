# $Id: Makefile,v 1.14 1995/11/25 00:59:17 davem Exp $
# Makefile for the linux Sparc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

# Also note that s4ctlb.o _must_ be the last object file

#.S.s:
#	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

all: mm.o
O_TARGET := mm.o
O_OBJS   := fault.o sun4c_vac.o init.o sun4c.o srmmu.o loadmmu.o mbus.o \
            s4cflush.o

#s4cflush.o: s4cflush.s

s4cflush.o: s4cflush.S
	$(CC) -D__ASSEMBLY__ -ansi -c $*.S -o $*.o

include $(TOPDIR)/Rules.make
