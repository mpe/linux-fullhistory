#
# Makefile for Econet support code.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

MOD_LIST_NAME := NET_MISC_MODULES

O_OBJS :=
M_OBJS :=

ifeq ($(CONFIG_ECONET),y)
  O_OBJS += econet.o
else
  ifeq ($(CONFIG_ECONET), m)
    M_OBJS += econet.o
  endif
endif

include $(TOPDIR)/Rules.make
