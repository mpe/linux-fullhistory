L_OBJS :=
M_OBJS :=
O_OBJS := module.o capi.o act2000_isa.o

O_TARGET :=
ifeq ($(CONFIG_ISDN_DRV_ACT2000),y)
  O_TARGET += act2000.o
else
  ifeq ($(CONFIG_ISDN_DRV_ACT2000),m)
  	O_TARGET += act2000.o
	M_OBJS = act2000.o
  endif
endif

include $(TOPDIR)/Rules.make
