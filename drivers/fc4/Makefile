# File: drivers/fc4/Makefile
#
# Makefile for the Linux Fibre Channel device drivers.
#

L_TARGET := fc4.a
M_OBJS   :=
MOD_LIST_NAME := FC4_MODULES

include ../../.config

ifeq ($(CONFIG_FC4),y)
  FC4 = fc.o
  ifeq ($(CONFIG_MODULES),y)
    O_TARGET := fc_n_syms.o
    O_OBJS   := fc.o
    OX_OBJS  := fc_syms.o
    FC4      := $(O_TARGET)
  endif
  L_OBJS += $(FC4)
else
  ifeq ($(CONFIG_FC4),m)
    MIX_OBJS += fc_syms.o
    M_OBJS += fc4.o
  endif
endif

ifeq ($(CONFIG_FC4_SOC),y)
L_OBJS += soc.o
else
  ifeq ($(CONFIG_FC4_SOC),m)
  M_OBJS += soc.o
  endif
endif

include $(TOPDIR)/Rules.make

fc4.o: $(MIX_OBJS) fc.o
	$(LD) $(LD_RFLAG) -r -o $@ $(MIX_OBJS) fc.o
