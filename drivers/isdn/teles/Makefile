L_OBJS :=
M_OBJS :=
O_OBJS := mod.o card.o config.o buffers.o tei.o isdnl2.o isdnl3.o \
llglue.o q931.o callc.o fsm.o

O_TARGET :=
ifeq ($(CONFIG_ISDN_DRV_TELES),y)
  O_TARGET += teles.o
else
  ifeq ($(CONFIG_ISDN_DRV_TELES),m)
    O_TARGET += teles.o
    M_OBJS += teles.o
  endif
endif

include $(TOPDIR)/Rules.make
 
