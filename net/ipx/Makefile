#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ipx.o
O_OBJS   := af_ipx.o
M_OBJS   := $(O_TARGET)

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_ipx.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
