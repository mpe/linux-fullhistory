SUB_DIRS     :=
MOD_SUB_DIRS :=
ALL_SUB_DIRS :=

L_OBJS :=
LX_OBJS :=
M_OBJS :=
MX_OBJS :=
O_OBJS :=
OX_OBJS :=
L_TARGET :=
O_TARGET :=

ifeq ($(CONFIG_PROC_FS),y)
  ifeq ($(CONFIG_HYSDN),y)
    M_OBJS += hysdn.o
    O_TARGET += hysdn.o
    O_OBJS += hysdn_procconf.o hysdn_proclog.o boardergo.o hysdn_boot.o hysdn_sched.o hysdn_net.o
  ifeq ($(CONFIG_HYSDN_CAPI),y)
    O_OBJS += hycapi.o
  endif
    OX_OBJS += hysdn_init.o 
  endif
endif

include $(TOPDIR)/Rules.make

