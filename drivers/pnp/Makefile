#
# Makefile for the kernel Plug-and-Play device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

ifeq ($(CONFIG_ISAPNP),m)
M_OBJS   := isa-pnp.o
endif

O_TARGET := isa-pnp.o
OX_OBJS  := isapnp.o
O_OBJS   := quirks.o

ifdef CONFIG_PROC_FS
O_OBJS   += isapnp_proc.o
endif

include $(TOPDIR)/Rules.make
