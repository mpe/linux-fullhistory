#
# Makefile for the Linux Bridge layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := bridge.o
O_OBJS	 := br.o br_tree.o
M_OBJS   := $(O_TARGET)

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_bridge.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
