# $Id: Makefile,v 1.1 1996/12/26 10:16:41 davem Exp $
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

all: kernel.o head.o initobj.o finitobj.o

O_TARGET := kernel.o
O_OBJS   := etrap.o rtrap.o signal32.o
OX_OBJS  := sparc64_ksyms.o

head.o: head.S
	$(CC) -D__ASSEMBLY__ -ansi -c $*.S -o $*.o

initobj.o: initobj.S
	$(CC) -D__ASSEMBLY__ -ansi -c initobj.S -o initobj.o

finitobj.o: initobj.S
	$(CC) -D__ASSEMBLY__ -ansi -c finitobj.S -o finitobj.o


include $(TOPDIR)/Rules.make
