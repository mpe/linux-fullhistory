#
# Makefile for the Linux IrDA IrLPT protocol layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

MOD_LIST_NAME := IRDA_MODULES
O_TARGET := irlpt.o
O_OBJS	 := irlpt_common.o
M_OBJS   := $(O_TARGET)
MI_OBJS  :=

OX_OBJS  += 

ifeq ($(CONFIG_IRLPT_CLIENT),y)
O_OBJS += irlpt_cli.o irlpt_cli_fsm.o
else
  ifeq ($(CONFIG_IRLPT_CLIENT),m)
  M_OBJS += irlpt_client.o
  endif
endif

ifeq ($(CONFIG_IRLPT_SERVER),y)
O_OBJS += irlpt_srvr.o irlpt_srvr_fsm.o
else
  ifeq ($(CONFIG_IRLPT_SERVER),m)
  M_OBJS += irlpt_server.o
  endif
endif

# Special rule to build the composite modules
ifeq ($(CONFIG_IRLPT_CLIENT),m)
irlpt_client.o: irlpt_cli.o irlpt_cli_fsm.o
	$(LD) $(LD_RFLAG) -r -o $@ irlpt_cli.o irlpt_cli_fsm.o
endif
ifeq ($(CONFIG_IRLPT_SERVER),m)
irlpt_server.o: irlpt_srvr.o irlpt_srvr_fsm.o
	$(LD) $(LD_RFLAG) -r -o $@ irlpt_srvr.o irlpt_srvr_fsm.o
endif

include $(TOPDIR)/Rules.make

tar:
	tar -cvf /dev/f1 .

