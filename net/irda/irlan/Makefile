#
# Makefile for the Linux IrDA IrLAN protocol layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := irlan.o
O_OBJS	 := irlan_common.o irlan_eth.o irlan_event.o irlan_client.o irlan_provider.o irlan_filter.o irlan_provider_event.o irlan_client_event.o
M_OBJS   := $(O_TARGET)
MI_OBJS  :=

OX_OBJS  += 

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
