SUB_DIRS     :=
MOD_SUB_DIRS :=
ALL_SUB_DIRS := icn teles pcbit

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
  ifdef CONFIG_ISDN_AUDIO
    L_OBJS += isdn_audio.o
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
    ifdef CONFIG_ISDN_AUDIO
      O_OBJS += isdn_audio.o
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

ifeq ($(CONFIG_ISDN_DRV_PCBIT),y)
  L_OBJS += pcbit/pcbit.o
  SUB_DIRS += pcbit
  MOD_SUB_DIRS += pcbit
else
  ifeq ($(CONFIG_ISDN_DRV_PCBIT),m)
    MOD_SUB_DIRS += pcbit
  endif
endif

include $(TOPDIR)/Rules.make

