L_OBJS :=
M_OBJS :=

ifeq ($(CONFIG_ISDN_DRV_LOOP),y)
  L_OBJS += isdnloop.o
else
  M_OBJS += isdnloop.o
endif

include $(TOPDIR)/Rules.make

