#
# Makefile for the ATM Protocol Families.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

include ../../.config

O_TARGET= atm.o

ifeq ($(CONFIG_ATM),y)

O_OBJS	= addr.o pvc.o signaling.o svc.o
OX_OBJS = common.o atm_misc.o raw.o resources.o

ifeq ($(CONFIG_ATM_CLIP),y)
O_OBJS += clip.o
NEED_IPCOM = ipcommon.o
endif

ifeq ($(CONFIG_NET_SCH_ATM),y)
NEED_IPCOM = ipcommon.o
endif

OX_OBJS += $(NEED_IPCOM)

ifeq ($(CONFIG_PROC_FS),y)
OX_OBJS += proc.o
endif

ifeq ($(CONFIG_ATM_LANE),y)
O_OBJS += lec.o
OX_OBJS += lane_mpoa_init.o
else
  ifeq ($(CONFIG_ATM_LANE),m)
  OX_OBJS += lane_mpoa_init.o
  M_OBJS += lec.o
  endif
endif

ifeq ($(CONFIG_ATM_MPOA),y)
O_OBJS += mpc.o mpoa_caches.o mpoa_proc.o
else
  ifeq ($(CONFIG_ATM_MPOA),m)
  M_OBJS += mpoa.o
  endif
endif

endif


include $(TOPDIR)/Rules.make

mpoa.o: mpc.o mpoa_caches.o mpoa_proc.o
	$(LD) -r -o mpoa.o mpc.o mpoa_caches.o mpoa_proc.o
