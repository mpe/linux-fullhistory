#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...


O_TARGET := ax25.o
O_OBJS	 := af_ax25.o sysctl_net_ax25.o

ifdef CONFIG_AX25
O_OBJS	+= ax25_in.o ax25_out.o ax25_route.o ax25_subr.o ax25_timer.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
