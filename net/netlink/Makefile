#
# Makefile for the netlink driver.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := netlink.o
MOD_LIST_NAME := NET_MISC_MODULES

O_OBJS  := 
OX_OBJS := af_netlink.o

M_OBJS :=

ifeq ($(CONFIG_NETLINK_DEV), y)
  O_OBJS += netlink_dev.o
endif

ifeq ($(CONFIG_NETLINK_DEV), m)
  M_OBJS += netlink_dev.o
endif

include $(TOPDIR)/Rules.make
