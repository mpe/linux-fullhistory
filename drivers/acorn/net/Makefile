# File: drivers/acorn/net/Makefile
#
# Makefile for the Acorn ethercard network device drivers
#

L_TARGET := acorn-net.a
L_OBJS   :=
M_OBJS   :=
MOD_LIST_NAME := ACORN_NET_MODULES

ifeq ($(CONFIG_ARM_ETHER1),y)
  L_OBJS += ether1.o
else
  ifeq ($(CONFIG_ARM_ETHER1),m)
    M_OBJS += ether1.o
  endif
endif

ifeq ($(CONFIG_ARM_ETHER3),y)
  L_OBJS += ether3.o
else
  ifeq ($(CONFIG_ARM_ETHER3),m)
    M_OBJS += ether3.o
  endif
endif

ifeq ($(CONFIG_ARM_ETHERH),y)
  L_OBJS += etherh.o
else
  ifeq ($(CONFIG_ARM_ETHERH),m)
    M_OBJS += etherh.o
  endif
endif

include $(TOPDIR)/Rules.make
