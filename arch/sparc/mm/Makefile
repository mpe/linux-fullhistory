# $Id: Makefile,v 1.35 1999/10/09 05:32:01 zaitcev Exp $
# Makefile for the linux Sparc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS   := fault.o init.o loadmmu.o generic.o extable.o btfixup.o
ifeq ($(CONFIG_AP1000),y)
O_OBJS	 += asyncd.o
endif
ifeq ($(CONFIG_SUN4),y)
O_OBJS	 += nosrmmu.o
else
O_OBJS	 += srmmu.o iommu.o io-unit.o hypersparc.o viking.o tsunami.o swift.o
endif
ifdef CONFIG_SMP
O_OBJS   += nosun4c.o
else
O_OBJS   += sun4c.o
endif

include $(TOPDIR)/Rules.make

hypersparc.o: hypersparc.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o hypersparc.o hypersparc.S

viking.o: viking.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o viking.o viking.S

tsunami.o: tsunami.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o tsunami.o tsunami.S

swift.o: swift.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o swift.o swift.S
