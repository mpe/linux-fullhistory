SUB_DIRS     :=
MOD_SUB_DIRS :=
ALL_SUB_DIRS := icn pcbit hisax avmb1 act2000 eicon divert hysdn

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
  L_OBJS += isdn_net.o isdn_tty.o isdn_cards.o isdn_v110.o
  LX_OBJS += isdn_common.o 
  ifdef CONFIG_ISDN_PPP
    L_OBJS += isdn_ppp.o
  endif
  ifdef CONFIG_ISDN_X25
    L_OBJS += isdn_x25iface.o
    L_OBJS += isdn_concap.o
  endif
  ifdef CONFIG_ISDN_AUDIO
    L_OBJS += isdn_audio.o
    ifdef CONFIG_ISDN_TTY_FAX
      L_OBJS += isdn_ttyfax.o
    endif
  endif
else
  ifeq ($(CONFIG_ISDN),m)
    M_OBJS += isdn.o
    O_TARGET += isdn.o
    O_OBJS += isdn_net.o isdn_tty.o isdn_v110.o
    OX_OBJS += isdn_common.o
    ifdef CONFIG_ISDN_PPP
      O_OBJS += isdn_ppp.o
      M_OBJS += isdn_bsdcomp.o
    endif
    ifdef CONFIG_ISDN_X25
      O_OBJS += isdn_x25iface.o
      O_OBJS += isdn_concap.o
    endif  
    ifdef CONFIG_ISDN_AUDIO
      O_OBJS += isdn_audio.o
      ifdef CONFIG_ISDN_TTY_FAX
        O_OBJS += isdn_ttyfax.o
      endif
    endif
  endif
endif

ifeq ($(CONFIG_ISDN_DIVERSION),y)
 ifeq ($(CONFIG_MODULES),y)
    MOD_SUB_DIRS += divert
 endif
endif

ifeq ($(CONFIG_ISDN_DRV_HISAX),y)
  L_OBJS += hisax/hisax.o
  SUB_DIRS += hisax
  MOD_SUB_DIRS += hisax
else
  ifeq ($(CONFIG_ISDN_DRV_HISAX),m)
    MOD_SUB_DIRS += hisax
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_ICN),y)
  L_OBJS += icn/icn_obj.o
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

ifeq ($(CONFIG_ISDN_DRV_SC),y)
  L_OBJS += sc/sc.o
  SUB_DIRS += sc
  MOD_SUB_DIRS += sc
else
  ifeq ($(CONFIG_ISDN_DRV_SC),m)
    MOD_SUB_DIRS += sc
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_AVMB1),y)
  L_OBJS += avmb1/avmb1.o
  SUB_DIRS += avmb1
  MOD_SUB_DIRS += avmb1
else
  ifeq ($(CONFIG_ISDN_DRV_AVMB1),m)
    MOD_SUB_DIRS += avmb1
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_LOOP),y)
  L_OBJS += isdnloop/isdnloop.o
  SUB_DIRS += isdnloop
  MOD_SUB_DIRS += isdnloop
else
  ifeq ($(CONFIG_ISDN_DRV_LOOP),m)
    MOD_SUB_DIRS += isdnloop
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_ACT2000),y)
  L_OBJS += act2000/act2000.o
  SUB_DIRS += act2000
  MOD_SUB_DIRS += act2000
else
  ifeq ($(CONFIG_ISDN_DRV_ACT2000),m)
    MOD_SUB_DIRS += act2000
  endif
endif

ifeq ($(CONFIG_ISDN_DRV_EICON),y)
  L_OBJS += eicon/eicon.o
  SUB_DIRS += eicon
  MOD_SUB_DIRS += eicon
else
  ifeq ($(CONFIG_ISDN_DRV_EICON),m)
    MOD_SUB_DIRS += eicon
  endif
endif

ifeq ($(CONFIG_HYSDN),y)
  MOD_SUB_DIRS += hysdn
endif

include $(TOPDIR)/Rules.make

