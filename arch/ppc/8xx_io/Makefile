#
# Makefile for the linux MPC8xx ppc-specific parts of comm processor
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := 8xx_io.a
O_OBJS	= commproc.o uart.o
ifdef CONFIG_MBX
O_OBJS += enet.o
endif
ifdef CONFIG_FADS
O_OBJS += fec.o
endif

include $(TOPDIR)/Rules.make
