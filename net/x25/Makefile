#
# Makefile for the Linux X.25 Packet layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := x25.o
O_OBJS	 := af_x25.o x25_dev.o x25_facilities.o x25_in.o x25_link.o x25_out.o \
	    x25_route.o x25_subr.o x25_timer.o
M_OBJS   := $(O_TARGET)

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_x25.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
