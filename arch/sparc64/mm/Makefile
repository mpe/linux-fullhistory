# $Id: Makefile,v 1.4 1997/07/24 12:15:08 davem Exp $
# Makefile for the linux Sparc64-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

ifdef SMP

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $< -o $*.o

else

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

endif

O_TARGET := mm.o
O_OBJS   := ultra.o fault.o init.o generic.o asyncd.o extable.o modutil.o

include $(TOPDIR)/Rules.make
