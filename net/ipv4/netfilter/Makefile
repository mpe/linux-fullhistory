#
# Makefile for the netfilter modules on top of IPv4.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := netfilter.o
M_OBJS :=

IP_NF_CONNTRACK_OBJ:=ip_conntrack_core.o ip_conntrack_proto_generic.o ip_conntrack_proto_tcp.o ip_conntrack_proto_udp.o ip_conntrack_proto_icmp.o

IP_NF_NAT_OBJ:=ip_nat_core.o ip_nat_proto_unknown.o ip_nat_proto_tcp.o ip_nat_proto_udp.o ip_nat_proto_icmp.o

# All the parts of conntrack and NAT required for compatibility layer.
IP_NF_COMPAT_LAYER:=ip_fw_compat.o ip_fw_compat_redir.o ip_fw_compat_masq.o $(IP_NF_CONNTRACK_OBJ) $(IP_NF_NAT_OBJ)

# Link order matters here.
ifeq ($(CONFIG_IP_NF_CONNTRACK),y)
OX_OBJS += ip_conntrack_standalone.o
O_OBJS += $(IP_NF_CONNTRACK_OBJ)
else
  ifeq ($(CONFIG_IP_NF_CONNTRACK),m)
  MI_OBJS += $(IP_NF_CONNTRACK_OBJ)
  MIX_OBJS += ip_conntrack_standalone.o
  M_OBJS += ip_conntrack.o
  endif
endif

ifeq ($(CONFIG_IP_NF_FTP),y)
OX_OBJS += ip_conntrack_ftp.o
else
  ifeq ($(CONFIG_IP_NF_FTP),m)
  MX_OBJS += ip_conntrack_ftp.o
  endif
endif

ifeq ($(CONFIG_IP_NF_IPTABLES),y)
OX_OBJS += ip_tables.o
else
  ifeq ($(CONFIG_IP_NF_IPTABLES),m)
  MX_OBJS += ip_tables.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_LIMIT),y)
O_OBJS += ipt_limit.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_LIMIT),m)
  M_OBJS += ipt_limit.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_MARK),y)
O_OBJS += ipt_mark.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_MARK),m)
  M_OBJS += ipt_mark.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_MAC),y)
O_OBJS += ipt_mac.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_MAC),m)
  M_OBJS += ipt_mac.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_MULTIPORT),y)
O_OBJS += ipt_multiport.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_MULTIPORT),m)
  M_OBJS += ipt_multiport.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_OWNER),y)
O_OBJS += ipt_owner.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_OWNER),m)
  M_OBJS += ipt_owner.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_TOS),y)
O_OBJS += ipt_tos.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_TOS),m)
  M_OBJS += ipt_tos.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_STATE),y)
O_OBJS += ipt_state.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_STATE),m)
  M_OBJS += ipt_state.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MATCH_UNCLEAN),y)
O_OBJS += ipt_unclean.o
else
  ifeq ($(CONFIG_IP_NF_MATCH_UNCLEAN),m)
  M_OBJS += ipt_unclean.o
  endif
endif

ifeq ($(CONFIG_IP_NF_NAT),y)
O_OBJS += ip_nat_standalone.o ip_nat_rule.o $(IP_NF_NAT_OBJ) 
  ifeq ($(CONFIG_IP_NF_FTP),y)
  O_OBJS += ip_nat_ftp.o
  endif
else
  ifeq ($(CONFIG_IP_NF_NAT),m)
  MI_OBJS += ip_nat_rule.o $(IP_NF_NAT_OBJ) 
  MIX_OBJS += ip_nat_standalone.o
  M_OBJS += iptable_nat.o
    ifeq ($(CONFIG_IP_NF_FTP),m)
    M_OBJS += ip_nat_ftp.o
    endif
  endif
endif

ifeq ($(CONFIG_IP_NF_FILTER),y)
O_OBJS += iptable_filter.o
else
  ifeq ($(CONFIG_IP_NF_FILTER),m)
  M_OBJS += iptable_filter.o
  endif
endif

ifeq ($(CONFIG_IP_NF_MANGLE),y)
O_OBJS += iptable_mangle.o
else
  ifeq ($(CONFIG_IP_NF_MANGLE),m)
  M_OBJS += iptable_mangle.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_REJECT),y)
O_OBJS += ipt_REJECT.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_REJECT),m)
  M_OBJS += ipt_REJECT.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_MIRROR),y)
O_OBJS += ipt_MIRROR.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_MIRROR),m)
  M_OBJS += ipt_MIRROR.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_TOS),y)
O_OBJS += ipt_TOS.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_TOS),m)
  M_OBJS += ipt_TOS.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_MARK),y)
O_OBJS += ipt_MARK.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_MARK),m)
  M_OBJS += ipt_MARK.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_MASQUERADE),y)
O_OBJS += ipt_MASQUERADE.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_MASQUERADE),m)
  M_OBJS += ipt_MASQUERADE.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_REDIRECT),y)
O_OBJS += ipt_REDIRECT.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_REDIRECT),m)
  M_OBJS += ipt_REDIRECT.o
  endif
endif

ifeq ($(CONFIG_IP_NF_TARGET_LOG),y)
O_OBJS += ipt_LOG.o
else
  ifeq ($(CONFIG_IP_NF_TARGET_LOG),m)
  M_OBJS += ipt_LOG.o
  endif
endif

ifeq ($(CONFIG_IP_NF_COMPAT_IPCHAINS),y)
O_OBJS += ipchains_core.o $(IP_NF_COMPAT_LAYER)
else
  ifeq ($(CONFIG_IP_NF_COMPAT_IPCHAINS),m)
  M_OBJS += ipchains.o
  endif
endif

ifeq ($(CONFIG_IP_NF_COMPAT_IPFWADM),y)
O_OBJS += ipfwadm_core.o $(IP_NF_COMPAT_LAYER)
else
  ifeq ($(CONFIG_IP_NF_COMPAT_IPFWADM),m)
  M_OBJS += ipfwadm.o
  endif
endif

ifeq ($(CONFIG_IP_NF_QUEUE),y)
O_OBJS += ip_queue.o
else
  ifeq ($(CONFIG_IP_NF_QUEUE),m)
  M_OBJS += ip_queue.o
  endif
endif

include $(TOPDIR)/Rules.make

ip_conntrack.o: ip_conntrack_standalone.o $(IP_NF_CONNTRACK_OBJ)
	$(LD) -r -o $@ $(IP_NF_CONNTRACK_OBJ) ip_conntrack_standalone.o 

iptable_nat.o: ip_nat_standalone.o ip_nat_rule.o $(IP_NF_NAT_OBJ) 
	$(LD) -r -o $@ ip_nat_standalone.o ip_nat_rule.o $(IP_NF_NAT_OBJ)

ipfwadm.o: ipfwadm_core.o $(IP_NF_COMPAT_LAYER)
	$(LD) -r -o $@ ipfwadm_core.o $(IP_NF_COMPAT_LAYER)

ipchains.o: ipchains_core.o $(IP_NF_COMPAT_LAYER) 
	$(LD) -r -o $@ ipchains_core.o $(IP_NF_COMPAT_LAYER)
