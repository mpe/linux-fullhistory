#
# Makefile for the Linux AX.25 layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...


O_TARGET := ax25.o
O_OBJS	 := ax25_addr.o ax25_dev.o ax25_iface.o ax25_in.o ax25_ip.o ax25_out.o \
	    ax25_route.o ax25_std_in.o ax25_std_subr.o ax25_std_timer.o \
	    ax25_subr.o ax25_timer.o ax25_uid.o
M_OBJS   := $(O_TARGET)

OX_OBJS  += af_ax25.o

ifeq ($(CONFIG_AX25_DAMA_SLAVE),y)
O_OBJS += ax25_ds_in.o ax25_ds_subr.o ax25_ds_timer.o
endif

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_ax25.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
