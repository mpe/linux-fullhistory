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

#CFLAGS_3c575_cb.o = -DCARDBUS
#CFLAGS_tulip_cb.o = -DCARDBUS

ifeq ($(CONFIG_PCMCIA_3C589),y)
  O_OBJS += 3c589_cs.o
else
  ifeq ($(CONFIG_PCMCIA_3C589),m)
    M_OBJS += 3c589_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_3C574),y)
  O_OBJS += 3c574_cs.o
else
  ifeq ($(CONFIG_PCMCIA_3C574),m)
    M_OBJS += 3c574_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_FMVJ18X),y)
  O_OBJS += fmvj18x_cs.o
else
  ifeq ($(CONFIG_PCMCIA_FMVJ18X),m)
    M_OBJS += fmvj18x_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_NMCLAN),y)
  O_OBJS += nmclan_cs.o
else
  ifeq ($(CONFIG_PCMCIA_NMCLAN),m)
    M_OBJS += nmclan_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_PCNET),y)
  O_OBJS += pcnet_cs.o
else
  ifeq ($(CONFIG_PCMCIA_PCNET),m)
    M_OBJS += pcnet_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_SMC91C92),y)
  O_OBJS += smc91c92_cs.o
else
  ifeq ($(CONFIG_PCMCIA_SMC91C92),m)
    M_OBJS += smc91c92_cs.o
  endif
endif

#ifeq ($(CONFIG_PCMCIA_3C575),y)
#  O_OBJS += 3c575_cb.o
#else
#  ifeq ($(CONFIG_PCMCIA_3C575),m)
#    M_OBJS += 3c575_cb.o
#  endif
#endif

#ifeq ($(CONFIG_PCMCIA_TULIP),y)
#  O_OBJS += tulip_cb.o
#else
#  ifeq ($(CONFIG_PCMCIA_TULIP),m)
#    M_OBJS += tulip_cb.o
#  endif
#endif

#ifeq ($(CONFIG_PCMCIA_EPIC100),y)
#  O_OBJS += epic100_cb.o
#else
#  ifeq ($(CONFIG_PCMCIA_EPIC100),m)
#    M_OBJS += epic100_cb.o
#  endif
#endif

ifeq ($(CONFIG_PCMCIA_RAYCS),y)
  OX_OBJS += ray_cs.o
else
  ifeq ($(CONFIG_PCMCIA_RAYCS),m)
  MX_OBJS += ray_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_NETWAVE),y)
  OX_OBJS += netwave_cs.o
else
  ifeq ($(CONFIG_PCMCIA_NETWAVE),m)
  M_OBJS += netwave_cs.o
  endif
endif

ifeq ($(CONFIG_PCMCIA_WAVELAN),y)
  OX_OBJS += wavelan_cs.o
else
  ifeq ($(CONFIG_PCMCIA_WAVELAN),m)
  M_OBJS += wavelan_cs.o
  endif
endif

include $(TOPDIR)/Rules.make

#epic100_cb.o: ../epic100.c
#	$(CC) $(CFLAGS) -DCARDBUS -c -o $@ ../epic100.c
