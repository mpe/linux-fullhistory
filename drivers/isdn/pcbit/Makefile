L_OBJS :=
M_OBJS :=
O_OBJS := module.o edss1.o drv.o layer2.o capi.o callbacks.o 

O_TARGET :=
ifeq ($(CONFIG_ISDN_DRV_PCBIT),y)
  O_TARGET += pcbit.o
else
  ifeq ($(CONFIG_ISDN_DRV_PCBIT),m)
    O_TARGET += pcbit.o
    M_OBJS += pcbit.o
  endif
endif

include $(TOPDIR)/Rules.make
