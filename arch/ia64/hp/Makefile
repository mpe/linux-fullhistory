#
# ia64/platform/hp/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

all: hp.a

O_TARGET	= hp.a
O_OBJS		= hpsim_console.o hpsim_irq.o hpsim_setup.o

ifeq ($(CONFIG_IA64_GENERIC),y)
O_OBJS		+= hpsim_machvec.o
endif

clean::

include $(TOPDIR)/Rules.make
