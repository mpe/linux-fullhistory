#
# Makefile for PARIDE
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

MOD_LIST_NAME := PARIDE_MODULES
L_TARGET := paride.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

ifeq ($(CONFIG_PARIDE),y)
  LX_OBJS += paride.o
else
  ifeq ($(CONFIG_PARIDE),m)
    MX_OBJS += paride.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PD),y)
  LX_OBJS += pd.o
else
  ifeq ($(CONFIG_PARIDE_PD),m)
    M_OBJS += pd.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PCD),y)
  LX_OBJS += pcd.o
else
  ifeq ($(CONFIG_PARIDE_PCD),m)
    M_OBJS += pcd.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PF),y)
  LX_OBJS += pf.o
else
  ifeq ($(CONFIG_PARIDE_PF),m)
    M_OBJS += pf.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PT),y)
  LX_OBJS += pt.o
else
  ifeq ($(CONFIG_PARIDE_PT),m)
    M_OBJS += pt.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PG),y)
  LX_OBJS += pg.o
else
  ifeq ($(CONFIG_PARIDE_PG),m)
    M_OBJS += pg.o
  endif
endif

ifeq ($(CONFIG_PARIDE_ATEN),y)
  LX_OBJS += aten.o
else
  ifeq ($(CONFIG_PARIDE_ATEN),m)
    M_OBJS += aten.o
  endif
endif

ifeq ($(CONFIG_PARIDE_BPCK),y)
  LX_OBJS += bpck.o
else
  ifeq ($(CONFIG_PARIDE_BPCK),m)
    M_OBJS += bpck.o
  endif
endif

ifeq ($(CONFIG_PARIDE_COMM),y)
  LX_OBJS += comm.o
else
  ifeq ($(CONFIG_PARIDE_COMM),m)
    M_OBJS += comm.o
  endif
endif

ifeq ($(CONFIG_PARIDE_DSTR),y)
  LX_OBJS += dstr.o
else
  ifeq ($(CONFIG_PARIDE_DSTR),m)
    M_OBJS += dstr.o
  endif
endif

ifeq ($(CONFIG_PARIDE_KBIC),y)
  LX_OBJS += kbic.o
else
  ifeq ($(CONFIG_PARIDE_KBIC),m)
    M_OBJS += kbic.o
  endif
endif

ifeq ($(CONFIG_PARIDE_EPAT),y)
  LX_OBJS += epat.o
else
  ifeq ($(CONFIG_PARIDE_EPAT),m)
    M_OBJS += epat.o
  endif
endif

ifeq ($(CONFIG_PARIDE_EPIA),y)
  LX_OBJS += epia.o
else
  ifeq ($(CONFIG_PARIDE_EPIA),m)
    M_OBJS += epia.o
  endif
endif

ifeq ($(CONFIG_PARIDE_FIT2),y)
  LX_OBJS += fit2.o
else
  ifeq ($(CONFIG_PARIDE_FIT2),m)
    M_OBJS += fit2.o
  endif
endif

ifeq ($(CONFIG_PARIDE_FIT3),y)
  LX_OBJS += fit3.o
else
  ifeq ($(CONFIG_PARIDE_FIT3),m)
    M_OBJS += fit3.o
  endif
endif

ifeq ($(CONFIG_PARIDE_FRPW),y)
  LX_OBJS += frpw.o
else
  ifeq ($(CONFIG_PARIDE_FRPW),m)
    M_OBJS += frpw.o
  endif
endif


ifeq ($(CONFIG_PARIDE_FRIQ),y)
  LX_OBJS += friq.o
else
  ifeq ($(CONFIG_PARIDE_FRIQ),m)
    M_OBJS += friq.o
  endif
endif

ifeq ($(CONFIG_PARIDE_ON20),y)
  LX_OBJS += on20.o
else
  ifeq ($(CONFIG_PARIDE_ON20),m)
    M_OBJS += on20.o
  endif
endif

ifeq ($(CONFIG_PARIDE_ON26),y)
  LX_OBJS += on26.o
else
  ifeq ($(CONFIG_PARIDE_ON26),m)
    M_OBJS += on26.o
  endif
endif

ifeq ($(CONFIG_PARIDE_KTTI),y)
  LX_OBJS += ktti.o
else
  ifeq ($(CONFIG_PARIDE_KTTI),m)
    M_OBJS += ktti.o
  endif
endif

include $(TOPDIR)/Rules.make

