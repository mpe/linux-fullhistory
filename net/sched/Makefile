#
# Makefile for the Linux Traffic Control Unit.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := sched.o

O_OBJS	:= sch_generic.o

ifeq ($(CONFIG_NET_SCHED), y)

O_OBJS += sch_api.o sch_fifo.o

ifeq ($(CONFIG_NET_ESTIMATOR), y)
O_OBJS += estimator.o
endif

ifeq ($(CONFIG_NET_CLS), y)
O_OBJS += cls_api.o

ifeq ($(CONFIG_NET_CLS_POLICE), y)
O_OBJS += police.o
endif

endif

ifeq ($(CONFIG_NET_SCH_CBQ), y)
O_OBJS += sch_cbq.o
else
  ifeq ($(CONFIG_NET_SCH_CBQ), m)
	M_OBJS += sch_cbq.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_CSZ), y)
O_OBJS += sch_csz.o
else
  ifeq ($(CONFIG_NET_SCH_CSZ), m)
	M_OBJS += sch_csz.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_HPFQ), y)
O_OBJS += sch_hpfq.o
else
  ifeq ($(CONFIG_NET_SCH_HPFQ), m)
	M_OBJS += sch_hpfq.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_HFSC), y)
O_OBJS += sch_hfsc.o
else
  ifeq ($(CONFIG_NET_SCH_HFSC), m)
	M_OBJS += sch_hfsc.o
  endif
endif


ifeq ($(CONFIG_NET_SCH_SFQ), y)
O_OBJS += sch_sfq.o
else
  ifeq ($(CONFIG_NET_SCH_SFQ), m)
	M_OBJS += sch_sfq.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_RED), y)
O_OBJS += sch_red.o
else
  ifeq ($(CONFIG_NET_SCH_RED), m)
	M_OBJS += sch_red.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_TBF), y)
O_OBJS += sch_tbf.o
else
  ifeq ($(CONFIG_NET_SCH_TBF), m)
	M_OBJS += sch_tbf.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_PRIO), y)
O_OBJS += sch_prio.o
else
  ifeq ($(CONFIG_NET_SCH_PRIO), m)
	M_OBJS += sch_prio.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_TEQL), y)
O_OBJS += sch_teql.o
else
  ifeq ($(CONFIG_NET_SCH_TEQL), m)
	M_OBJS += sch_teql.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_U32), y)
O_OBJS += cls_u32.o
else
  ifeq ($(CONFIG_NET_CLS_U32), m)
	M_OBJS += cls_u32.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_RSVP), y)
O_OBJS += cls_rsvp.o
else
  ifeq ($(CONFIG_NET_CLS_RSVP), m)
	M_OBJS += cls_rsvp.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_RSVP6), y)
O_OBJS += cls_rsvp6.o
else
  ifeq ($(CONFIG_NET_CLS_RSVP6), m)
	M_OBJS += cls_rsvp6.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_ROUTE), y)
O_OBJS += cls_route.o
endif

ifeq ($(CONFIG_NET_CLS_FW), y)
O_OBJS += cls_fw.o
endif

endif

include $(TOPDIR)/Rules.make
