# $Id: Makefile,v 1.30 1998/03/09 14:03:53 jj Exp $
# Makefile for the linux Sparc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS   := fault.o init.o loadmmu.o generic.o asyncd.o extable.o btfixup.o
ifeq ($(CONFIG_SUN4),y)
O_OBJS	 += nosrmmu.o
else
O_OBJS	 += srmmu.o iommu.o io-unit.o hypersparc.o viking.o tsunami.o turbosparc.o
endif
ifdef SMP
O_OBJS   += nosun4c.o
else
O_OBJS   += sun4c.o
endif

include $(TOPDIR)/Rules.make

hypersparc.o: hypersparc.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o hypersparc.o hypersparc.S

turbosparc.o: turbosparc.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o turbosparc.o turbosparc.S

viking.o: viking.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o viking.o viking.S

tsunami.o: tsunami.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o tsunami.o tsunami.S
