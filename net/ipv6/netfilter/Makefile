#
# Makefile for the netfilter modules on top of IPv6.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := netfilter.o
MOD_LIST_NAME := IPV6_MODULES
M_OBJS :=

IP6_NF_CONNTRACK_OBJ:=ip6_conntrack_core.o ip6_conntrack_proto_generic.o ip6_conntrack_proto_tcp.o ip6_conntrack_proto_udp.o ip6_conntrack_proto_icmp.o

# Link order matters here.
ifeq ($(CONFIG_IP6_NF_CONNTRACK),y)
O_OBJS += ip6_conntrack_standalone.o $(IP6_NF_CONNTRACK_OBJ)
else
  ifeq ($(CONFIG_IP6_NF_CONNTRACK),m)
  MI_OBJS += $(IP6_NF_CONNTRACK_OBJ)
  MIX_OBJS += ip6_conntrack_standalone.o
  M_OBJS += ip6_conntrack.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_FTP),y)
O_OBJS += ip6_conntrack_ftp.o
else
  ifeq ($(CONFIG_IP6_NF_FTP),m)
  MX_OBJS += ip6_conntrack_ftp.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_IPTABLES),y)
O_OBJS += ip6_tables.o
else
  ifeq ($(CONFIG_IP6_NF_IPTABLES),m)
  MX_OBJS += ip6_tables.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_LIMIT),y)
O_OBJS += ip6t_limit.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_LIMIT),m)
  M_OBJS += ip6t_limit.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_MARK),y)
O_OBJS += ip6t_mark.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_MARK),m)
  M_OBJS += ip6t_mark.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_MAC),y)
O_OBJS += ip6t_mac.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_MAC),m)
  M_OBJS += ip6t_mac.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_MULTIPORT),y)
O_OBJS += ip6t_multiport.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_MULTIPORT),m)
  M_OBJS += ip6t_multiport.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_OWNER),y)
O_OBJS += ip6t_owner.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_OWNER),m)
  M_OBJS += ip6t_owner.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_TOS),y)
O_OBJS += ip6t_tos.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_TOS),m)
  M_OBJS += ip6t_tos.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_STATE),y)
O_OBJS += ip6t_state.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_STATE),m)
  M_OBJS += ip6t_state.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_MATCH_UNCLEAN),y)
O_OBJS += ip6t_unclean.o
else
  ifeq ($(CONFIG_IP6_NF_MATCH_UNCLEAN),m)
  M_OBJS += ip6t_unclean.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_FILTER),y)
O_OBJS += ip6table_filter.o
else
  ifeq ($(CONFIG_IP6_NF_FILTER),m)
  M_OBJS += ip6table_filter.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_TARGET_REJECT),y)
O_OBJS += ip6t_REJECT.o
else
  ifeq ($(CONFIG_IP6_NF_TARGET_REJECT),m)
  M_OBJS += ip6t_REJECT.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_TARGET_MIRROR),y)
O_OBJS += ip6t_MIRROR.o
else
  ifeq ($(CONFIG_IP6_NF_TARGET_MIRROR),m)
  M_OBJS += ip6t_MIRROR.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_TARGET_TOS),y)
O_OBJS += ip6t_TOS.o
else
  ifeq ($(CONFIG_IP6_NF_TARGET_TOS),m)
  M_OBJS += ip6t_TOS.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_TARGET_MARK),y)
O_OBJS += ip6t_MARK.o
else
  ifeq ($(CONFIG_IP6_NF_TARGET_MARK),m)
  M_OBJS += ip6t_MARK.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_TARGET_REDIRECT),y)
O_OBJS += ip6t_REDIRECT.o
else
  ifeq ($(CONFIG_IP6_NF_TARGET_REDIRECT),m)
  M_OBJS += ip6t_REDIRECT.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_TARGET_LOG),y)
O_OBJS += ip6t_LOG.o
else
  ifeq ($(CONFIG_IP6_NF_TARGET_LOG),m)
  M_OBJS += ip6t_LOG.o
  endif
endif

ifeq ($(CONFIG_IP6_NF_QUEUE),y)
O_OBJS += ip6_queue.o
else
  ifeq ($(CONFIG_IP6_NF_QUEUE),m)
  M_OBJS += ip6_queue.o
  endif
endif

include $(TOPDIR)/Rules.make

ip6_conntrack.o: ip6_conntrack_standalone.o $(IP6_NF_CONNTRACK_OBJ)
	$(LD) -r -o $@ $(IP6_NF_CONNTRACK_OBJ) ip6_conntrack_standalone.o 

ip6fwadm.o: ipfwadm_core.o $(IP6_NF_COMPAT_LAYER)
	$(LD) -r -o $@ ip6fwadm_core.o $(IP6_NF_COMPAT_LAYER)

ip6chains.o: ip6chains_core.o $(IP6_NF_COMPAT_LAYER) 
	$(LD) -r -o $@ ip6chains_core.o $(IP6_NF_COMPAT_LAYER)
