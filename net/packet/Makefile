#
# Makefile for the packet AF.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := packet.o
MOD_LIST_NAME := NET_MISC_MODULES

O_OBJS :=
M_OBJS :=

ifeq ($(CONFIG_PACKET),y)
  O_OBJS += af_packet.o
else
  ifeq ($(CONFIG_PACKET), m)
    M_OBJS += af_packet.o
  endif
endif

include $(TOPDIR)/Rules.make
