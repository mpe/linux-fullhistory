#
# Makefile for the linux networking.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

MOD_SUB_DIRS := ipv4
ALL_SUB_DIRS := 802 ax25 bridge core ethernet ipv4 ipx unix appletalk netrom #decnet
SUB_DIRS     := core ethernet unix
MOD_LIST_NAME := NET_MISC_MODULES

ifeq ($(CONFIG_NET),y)
SUB_DIRS += 802
endif

ifeq ($(CONFIG_INET),y)
SUB_DIRS += ipv4
endif

ifeq ($(CONFIG_BRIDGE),y)
SUB_DIRS += bridge
endif

ifeq ($(CONFIG_IPX),y)
SUB_DIRS += ipx
else
  ifeq ($(CONFIG_IPX),m)
  MOD_SUB_DIRS += ipx
  endif
endif

ifeq ($(CONFIG_ATALK),y)
SUB_DIRS += appletalk
else
  ifeq ($(CONFIG_ATALK),m)
  MOD_SUB_DIRS += appletalk
  endif
endif

ifeq ($(CONFIG_NETROM),y)
SUB_DIRS += netrom
endif

ifeq ($(CONFIG_AX25),y)
SUB_DIRS += ax25
endif

L_TARGET     := network.a
L_OBJS	     := socket.o protocols.o sysctl_net.o $(join $(SUB_DIRS),$(SUB_DIRS:%=/%.o))
ifeq ($(CONFIG_NET),y)
ifeq ($(CONFIG_MODULES),y)
LX_OBJS = netsyms.o
endif
endif

M_OBJS	     :=

ifeq ($(CONFIG_NETLINK),y)
L_OBJS += netlink.o
else
  ifeq ($(CONFIG_NETLINK),m)
    M_OBJS += netlink.o
  endif
endif

include $(TOPDIR)/Rules.make
