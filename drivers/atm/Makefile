# File: drivers/atm/Makefile
#
# Makefile for the Linux network (ATM) device drivers.
#

L_TARGET := atm.a
L_OBJS	 := atmdev_init.o
M_OBJS	 :=
MOD_LIST_NAME := ATM_MODULES

include ../../.config

ifeq ($(CONFIG_ATM_ENI),y)
L_OBJS += eni.o
NEED_SUNI_LX = suni.o
else
  ifeq ($(CONFIG_ATM_ENI),m)
  M_OBJS += eni.o
  NEED_SUNI_MX = suni.o
  endif
endif

ifeq ($(CONFIG_ATM_ZATM),y)
L_OBJS += zatm.o
LX_OBJS += uPD98402.o
else
  ifeq ($(CONFIG_ATM_ZATM),m)
  M_OBJS += zatm.o
  MX_OBJS += uPD98402.o
  endif
endif

ifeq ($(CONFIG_ATM_TNETA1570),y)
L_OBJS += tneta1570.o suni.o
endif

ifeq ($(CONFIG_ATM_FORE200),y)
L_OBJS += fore200.o
endif

ifeq ($(CONFIG_ATM_NICSTAR),y)
L_OBJS += nicstar.o
  ifeq ($(CONFIG_ATM_NICSTAR_USE_SUNI),y)
  NEED_SUNI_LX = suni.o
  endif
else
  ifeq ($(CONFIG_ATM_NICSTAR),m)
  M_OBJS += nicstar.o
    ifeq ($(CONFIG_ATM_NICSTAR_USE_SUNI),y)
    NEED_SUNI_MX = suni.o
    endif
  endif
endif

ifeq ($(CONFIG_ATM_HORIZON),y)
L_OBJS += horizon.o
else
  ifeq ($(CONFIG_ATM_HORIZON),m)
  M_OBJS += horizon.o
  endif
endif

ifeq ($(CONFIG_ATM_AMBASSADOR),y)
L_OBJS += ambassador.o
else
  ifeq ($(CONFIG_ATM_AMBASSADOR),m)
  M_OBJS += ambassador.o
  endif
endif

ifeq ($(NEED_SUNI_LX),)
  MX_OBJS += $(NEED_SUNI_MX)
else
  LX_OBJS += $(NEED_SUNI_LX)
endif

ifeq ($(CONFIG_ATM_TCP),y)
L_OBJS += atmtcp.o
else
  ifeq ($(CONFIG_ATM_TCP),m)
  M_OBJS += atmtcp.o
  endif
endif

EXTRA_CFLAGS=-g

include $(TOPDIR)/Rules.make
