#
# Makefile for the linux networking.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

MOD_SUB_DIRS := ipv4
ALL_SUB_DIRS := 802 ax25 bridge core ethernet ipv4 ipv6 ipx unix appletalk \
		netrom rose lapb x25 wanrouter sunrpc #decnet
SUB_DIRS     := core ethernet unix
MOD_LIST_NAME := NET_MISC_MODULES

ifeq ($(CONFIG_NET),y)
SUB_DIRS += 802
endif

ifeq ($(CONFIG_INET),y)
SUB_DIRS += ipv4
endif

ifeq ($(CONFIG_IPV6),y)
SUB_DIRS += ipv6
else
  ifeq ($(CONFIG_IPV6),m)
  MOD_SUB_DIRS += ipv6
  endif
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

ifeq ($(CONFIG_WAN_ROUTER),y)
SUB_DIRS += wanrouter
else
  ifeq ($(CONFIG_WAN_ROUTER),m)
  MOD_SUB_DIRS += wanrouter
  endif
endif

ifeq ($(CONFIG_X25),y)
SUB_DIRS += x25
else
  ifeq ($(CONFIG_X25),m)
  MOD_SUB_DIRS += x25
  endif
endif

ifeq ($(CONFIG_LAPB),y)
SUB_DIRS += lapb
else
  ifeq ($(CONFIG_LAPB),m)
  MOD_SUB_DIRS += lapb
  endif
endif

ifeq ($(CONFIG_NETROM),y)
SUB_DIRS += netrom
else
  ifeq ($(CONFIG_NETROM),m)
  MOD_SUB_DIRS += netrom
  endif
endif

ifeq ($(CONFIG_ROSE),y)
SUB_DIRS += rose
else
  ifeq ($(CONFIG_ROSE),m)
  MOD_SUB_DIRS += rose
  endif
endif

ifeq ($(CONFIG_AX25),y)
SUB_DIRS += ax25
else
  ifeq ($(CONFIG_AX25),m)
  MOD_SUB_DIRS += ax25
  endif
endif

ifeq ($(CONFIG_SUNRPC),y)
SUB_DIRS += sunrpc
else
  ifeq ($(CONFIG_SUNRPC),m)
  MOD_SUB_DIRS += sunrpc
  endif
endif

# We must attach netsyms.o to socket.o, as otherwise there is nothing
# to pull the object file from the archive.

SOCK         := socket.o
ifeq ($(CONFIG_NET),y)
ifeq ($(CONFIG_MODULES),y)
O_TARGET     := sock_n_syms.o
O_OBJS       := socket.o
OX_OBJS      := netsyms.o
SOCK         := $(O_TARGET)
endif
endif

L_TARGET     := network.a
L_OBJS	     := $(SOCK) protocols.o $(join $(SUB_DIRS),$(SUB_DIRS:%=/%.o))

M_OBJS	     :=

ifeq ($(CONFIG_SYSCTL),y)
L_OBJS += sysctl_net.o
endif

CONFIG_NETLINK_BUILTIN :=
CONFIG_NETLINK_MODULE :=

ifeq ($(CONFIG_NETLINK), y)
  CONFIG_NETLINK_BUILTIN = y
endif

ifeq ($(CONFIG_IPV6), y)
  CONFIG_NETLINK_BUILTIN = y
endif

ifeq ($(CONFIG_NETLINK), m)
  CONFIG_NETLINK_MODULE = y
endif

ifeq ($(CONFIG_IPV6), m)
  CONFIG_NETLINK_MODULE = y
endif

ifdef CONFIG_NETLINK_BUILTIN
L_OBJS += netlink.o
else
  ifdef CONFIG_NETLINK_MODULE
    M_OBJS += netlink.o
  endif
endif

include $(TOPDIR)/Rules.make
