#
# Makefile for the Linux unix domain socket layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := unix.o
O_OBJS	 := af_unix.o garbage.o
M_OBJS   := $(O_TARGET)

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_unix.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
