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

L_TARGET := pnp.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

ifeq ($(CONFIG_ISAPNP),y)
  LX_OBJS += isapnp.o quirks.o
else
  ifeq ($(CONFIG_ISAPNP),m)
    MX_OBJS += isapnp.o quirks.o
  endif
endif

include $(TOPDIR)/Rules.make
