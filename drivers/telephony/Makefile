#
# Makefile for the kernel miscellaneous drivers.
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

L_TARGET := telephony.a
MX_OBJS  :=
M_OBJS  :=

ifeq ($(CONFIG_PHONE),y)
  LX_OBJS += phonedev.o
else
  ifeq ($(CONFIG_PHONE),m)
    MX_OBJS += phonedev.o
  endif
endif

ifeq ($(CONFIG_PHONE_IXJ),y)
  L_OBJS += ixj.o
else 
  ifeq ($(CONFIG_PHONE_IXJ),m)
    M_OBJS += ixj.o
  endif
endif

include $(TOPDIR)/Rules.make
