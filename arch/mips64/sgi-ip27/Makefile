# $Id: Makefile,v 1.3 2000/02/04 08:41:40 kanoj Exp $
#
# Makefile for the IP27 specific kernel interface routines under Linux.
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = ip27.a
L_OBJS  = ip27-berr.o ip27-irq.o ip27-irq-glue.o ip27-klconfig.o \
	  ip27-memory.o ip27-pci.o ip27-pci-dma.o ip27-reset.o ip27-setup.o \
	  ip27-timer.o ip27-init.o ip27-nmi.o

include $(TOPDIR)/Rules.make
