#
# Makefile for the Linux IrDA IrLAN protocol layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

MOD_LIST_NAME := IRDA_MODULES
O_TARGET := irlan.o
O_OBJS	 := irlan_common.o irlan_eth.o irlan_event.o
M_OBJS   := $(O_TARGET)
MI_OBJS  :=

OX_OBJS  += 

ifeq ($(CONFIG_IRLAN_CLIENT),y)
O_OBJS += irlan_cli.o irlan_cli_event.o
else
  ifeq ($(CONFIG_IRLAN_CLIENT),m)
#  MI_OBJS += irlan_cli.o irlan_cli_event.o
  M_OBJS += irlan_client.o
  endif
endif

ifeq ($(CONFIG_IRLAN_SERVER),y)
O_OBJS += irlan_srv.o irlan_srv_event.o
else
  ifeq ($(CONFIG_IRLAN_SERVER),m)
#  MI_OBJS += irlan_srv.o irlan_srv_event.o
  M_OBJS += irlan_server.o
  endif
endif

# Special rule to build the composite modules
ifeq ($(CONFIG_IRLAN),m)
irlan_client.o: irlan_cli.o irlan_cli_event.o
	$(LD) $(LD_RFLAG) -r -o $@ irlan_cli.o irlan_cli_event.o

irlan_server.o: irlan_srv.o irlan_srv_event.o
	$(LD) $(LD_RFLAG) -r -o $@ irlan_srv.o irlan_srv_event.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .




