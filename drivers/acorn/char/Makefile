#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

L_TARGET := acorn-char.a
M_OBJS   :=
L_OBJS   :=

ifeq ($(CONFIG_ATOMWIDE_SERIAL),y)
  L_OBJS += serial-atomwide.o
else
  ifeq ($(CONFIG_ATOMWIDE_SERIAL),m)
    M_OBJS += serial-atomwide.o
  endif
endif

ifeq ($(CONFIG_DUALSP_SERIAL),y)
  L_OBJS += serial-dualsp.o
else
  ifeq ($(CONFIG_DUALSP_SERIAL),m)
    M_OBJS += serial-dualsp.o
  endif
endif

include $(TOPDIR)/Rules.make
