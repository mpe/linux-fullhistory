SUB_DIRS     :=
MOD_SUB_DIRS :=
ALL_SUB_DIRS := icn teles

L_OBJS :=
LX_OBJS :=
M_OBJS :=
MX_OBJS :=
O_OBJS :=
OX_OBJS :=
L_TARGET :=
O_TARGET :=

ifeq ($(CONFIG_ISDN),y)
  L_TARGET := isdn.a
  L_OBJS += isdn_net.o isdn_tty.o isdn_cards.o
  LX_OBJS += isdn_common.o
  ifdef CONFIG_ISDN_PPP
    L_OBJS += isdn_ppp.o
  endif
else
  ifeq ($(CONFIG_ISDN),m)
    M_OBJS += isdn.o
    O_TARGET += isdn.o
    O_OBJS += isdn_net.o isdn_tty.o
    OX_OBJS += isdn_common.o
    ifdef CONFIG_ISDN_PPP
      O_OBJS += isdn_ppp.o
    endif
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_TELES),y)
  L_OBJS += teles/teles.o
  SUB_DIRS += teles
  MOD_SUB_DIRS += teles
else
  ifeq ($(CONFIG_ISDN_DRV_TELES),m)
    MOD_SUB_DIRS += teles
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_ICN),y)
  L_OBJS += icn/icn.o
  SUB_DIRS += icn
  MOD_SUB_DIRS += icn
else
  ifeq ($(CONFIG_ISDN_DRV_ICN),m)
    MOD_SUB_DIRS += icn
  endif
endif

include $(TOPDIR)/Rules.make

