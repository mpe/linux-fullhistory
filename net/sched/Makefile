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


ifeq ($(CONFIG_NET_SCH_PFIFO), y)
O_OBJS += sch_fifo.o
else
  ifeq ($(CONFIG_NET_SCH_PFIFO), m)
	M_OBJS += sch_fifo.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_PRIO), y)
O_OBJS += sch_prio.o
else
  ifeq ($(CONFIG_NET_SCH_PRIO), m)
	M_OBJS += sch_prio.o
  endif
endif

include $(TOPDIR)/Rules.make
