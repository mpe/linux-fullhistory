# $Id: Makefile,v 1.22 1996/11/22 11:57:03 ecd Exp $
# Makefile for the linux Sparc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS   := fault.o init.o sun4c.o srmmu.o loadmmu.o generic.o asyncd.o \
	    extable.o

include $(TOPDIR)/Rules.make
