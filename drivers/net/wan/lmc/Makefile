# File: drivers/lmc/Makefile
#
# Makefile for the Lan Media 21140 based WAN cards
# Specifically the 1000,1200,5200,5245
#

ifeq ($(CONFIG_LANMEDIA),y)
    O_TARGET := lmc.o
    O_OBJS = lmc_debug.o lmc_media.o lmc_main.o lmc_proto.o
else
  ifeq ($(CONFIG_LANMEDIA),m)
    MOD_LIST_NAME := NET_MODULES
    M_OBJS := lmc.o
    O_TARGET := lmc.o
    O_OBJS = lmc_debug.o lmc_media.o lmc_main.o lmc_proto.o
  endif
endif

#
# Base debugging and event log (doubles lmc.o size)
#
# DBGDEF =  \
# -DDEBUG

#
# Like above except every packet gets echoed to KERN_DEBUG
# in hex
#
# DBDEF = \
# -DDEBUG \
# -DLMC_PACKET_LOG

EXTRA_CFLAGS += -I. $(DBGDEF)

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

