#
# Makefile for the nubus specific drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

L_TARGET := nubus.a

ifeq ($(CONFIG_MODULES),y)
O_TARGET := nubus_n_syms.o
OX_OBJS  := nubus_syms.o
O_OBJS   := nubus.o
L_OBJS   := nubus_n_syms.o
else
L_OBJS   := nubus.o
endif

ifdef CONFIG_PROC_FS
L_OBJS   += proc.o
endif

include $(TOPDIR)/Rules.make
