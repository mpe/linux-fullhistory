# $Id: Makefile,v 1.26 1997/06/24 15:48:06 jj Exp $
# Makefile for the linux Sparc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS   := fault.o init.o sun4c.o srmmu.o hypersparc.o viking.o \
	    tsunami.o loadmmu.o generic.o asyncd.o extable.o \
	    turbosparc.o

include $(TOPDIR)/Rules.make

ifdef SMP

hypersparc.o: hypersparc.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o hypersparc.o hypersparc.S

turbosparc.o: turbosparc.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o turbosparc.o turbosparc.S

viking.o: viking.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o viking.o viking.S

tsunami.o: tsunami.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o tsunami.o tsunami.S

else

hypersparc.o: hypersparc.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o hypersparc.o hypersparc.S

turbosparc.o: turbosparc.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o turbosparc.o turbosparc.S

viking.o: viking.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o viking.o viking.S

tsunami.o: tsunami.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o tsunami.o tsunami.S

endif
