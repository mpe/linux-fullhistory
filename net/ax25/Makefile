#
# Makefile for the Linux AX.25 layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...


O_TARGET := ax25.o
O_OBJS	 := sysctl_net_ax25.o ax25_in.o ax25_out.o ax25_route.o ax25_subr.o ax25_timer.o
M_OBJS   := $(O_TARGET)

OX_OBJS  += af_ax25.o

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
