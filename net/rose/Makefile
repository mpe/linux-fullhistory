#
# Makefile for the Linux Rose (X.25 PLP) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := rose.o
O_OBJS	 := af_rose.o rose_dev.o rose_in.o rose_link.o rose_loopback.o \
	    rose_out.o rose_route.o rose_subr.o rose_timer.o
M_OBJS   := $(O_TARGET)

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_rose.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
