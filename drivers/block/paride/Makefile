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
    MX_OBJS += pd.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PCD),y)
  LX_OBJS += pcd.o
else
  ifeq ($(CONFIG_PARIDE_PCD),m)
    MX_OBJS += pcd.o
  endif
endif

ifeq ($(CONFIG_PARIDE_PF),y)
  LX_OBJS += pf.o
else
  ifeq ($(CONFIG_PARIDE_PF),m)
    MX_OBJS += pf.o
  endif
endif

ifeq ($(CONFIG_PARIDE_ATEN),y)
  LX_OBJS += aten.o
else
  ifeq ($(CONFIG_PARIDE_ATEN),m)
    MX_OBJS += aten.o
  endif
endif

ifeq ($(CONFIG_PARIDE_BPCK),y)
  LX_OBJS += bpck.o
else
  ifeq ($(CONFIG_PARIDE_BPCK),m)
    MX_OBJS += bpck.o
  endif
endif

ifeq ($(CONFIG_PARIDE_COMM),y)
  LX_OBJS += comm.o
else
  ifeq ($(CONFIG_PARIDE_COMM),m)
    MX_OBJS += comm.o
  endif
endif

ifeq ($(CONFIG_PARIDE_DSTR),y)
  LX_OBJS += dstr.o
else
  ifeq ($(CONFIG_PARIDE_DSTR),m)
    MX_OBJS += dstr.o
  endif
endif

ifeq ($(CONFIG_PARIDE_KBIC),y)
  LX_OBJS += kbic.o
else
  ifeq ($(CONFIG_PARIDE_KBIC),m)
    MX_OBJS += kbic.o
  endif
endif

ifeq ($(CONFIG_PARIDE_EPAT),y)
  LX_OBJS += epat.o
else
  ifeq ($(CONFIG_PARIDE_EPAT),m)
    MX_OBJS += epat.o
  endif
endif

ifeq ($(CONFIG_PARIDE_EPIA),y)
  LX_OBJS += epia.o
else
  ifeq ($(CONFIG_PARIDE_EPIA),m)
    MX_OBJS += epia.o
  endif
endif

ifeq ($(CONFIG_PARIDE_FRPW),y)
  LX_OBJS += frpw.o
else
  ifeq ($(CONFIG_PARIDE_FRPW),m)
    MX_OBJS += frpw.o
  endif
endif

ifeq ($(CONFIG_PARIDE_ON20),y)
  LX_OBJS += on20.o
else
  ifeq ($(CONFIG_PARIDE_ON20),m)
    MX_OBJS += on20.o
  endif
endif

ifeq ($(CONFIG_PARIDE_ON26),y)
  LX_OBJS += on26.o
else
  ifeq ($(CONFIG_PARIDE_ON26),m)
    MX_OBJS += on26.o
  endif
endif

include $(TOPDIR)/Rules.make
