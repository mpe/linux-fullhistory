#
# Makefile for the linux MPC8xx ppc-specific parts of comm processor
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := 8260_io.a
O_OBJS	= commproc.o uart.o

ifdef CONFIG_FCC_ENET
O_OBJS += fcc.o
endif
ifdef CONFIG_SCC_ENET
O_OBJS += enet.o
endif

include $(TOPDIR)/Rules.make
