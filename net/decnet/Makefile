# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := decnet.o
O_OBJS   := af_decnet.o dn_nsp_in.o dn_nsp_out.o dn_route.o dn_dev.o dn_neigh.o dn_timer.o
M_OBJS	 := $(O_TARGET)

ifeq ($(CONFIG_DECNET_ROUTER),y)
O_OBJS += dn_fib.o dn_rules.o dn_table.o
endif

ifeq ($(CONFIG_DECNET_FW),y)
O_OBJS += dn_fw.o
endif

O_OBJS += sysctl_net_decnet.o

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
