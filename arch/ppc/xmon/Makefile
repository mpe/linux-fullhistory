# Makefile for xmon

O_TARGET = x.o
ifeq ($(CONFIG_8xx),y)
O_OBJS = start_8xx.o xmon.o ppc-dis.o ppc-opc.o subr_prf.o setjmp.o
else
O_OBJS = start.o xmon.o ppc-dis.o ppc-opc.o subr_prf.o setjmp.o
endif

include $(TOPDIR)/Rules.make
