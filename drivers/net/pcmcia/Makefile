#
# drivers/net/pcmcia/Makefile
#
# Makefile for the Linux PCMCIA network device drivers.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := pcmcia_net.o
O_OBJS   :=
M_OBJS   :=
MOD_LIST_NAME := PCMCIA_MODULES

ifeq ($(CONFIG_PCMCIA_PCNET),y)
  O_OBJS += pcnet_cs.o
else
  ifeq ($(CONFIG_PCMCIA_PCNET),m)
    MX_OBJS += pcnet_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_3C589),y)
  O_OBJS += 3c589_cs.o
else
  ifeq ($(CONFIG_PCMCIA_3C589),m)
    MX_OBJS += 3c589_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_RAYCS),y)
  OX_OBJS += ray_cs.o
else
  ifeq ($(CONFIG_PCMCIA_RAYCS),m)
  MX_OBJS += ray_cs.o
  endif
endif

include $(TOPDIR)/Rules.make
