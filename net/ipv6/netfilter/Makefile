#
# Makefile for the netfilter modules on top of IPv6.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := netfilter.o

multi-objs := ip6_conntrack.o
export-objs := ip6_conntrack_standalone.o

ip6_conntrack-obj := ip6_conntrack_standalone.o ip6_conntrack_core.o ip6_conntrack_proto_generic.o ip6_conntrack_proto_tcp.o ip6_conntrack_proto_udp.o ip6_conntrack_proto_icmp.o

# Link order matters here.
obj-$(CONFIG_IP6_NF_CONNTRACK) += ip6_conntrack.o
obj-$(CONFIG_IP6_NF_FTP) += ip6_conntrack_ftp.o
obj-$(CONFIG_IP6_NF_IPTABLES) += ip6_tables.o
obj-$(CONFIG_IP6_NF_MATCH_LIMIT) += ip6t_limit.o
obj-$(CONFIG_IP6_NF_MATCH_MARK) += ip6t_mark.o
obj-$(CONFIG_IP6_NF_MATCH_MAC) += ip6t_mac.o
obj-$(CONFIG_IP6_NF_MATCH_MULTIPORT) += ip6t_multiport.o
obj-$(CONFIG_IP6_NF_MATCH_OWNER) += ip6t_owner.o
obj-$(CONFIG_IP6_NF_MATCH_TOS) += ip6t_tos.o
obj-$(CONFIG_IP6_NF_MATCH_STATE) += ip6t_state.o
obj-$(CONFIG_IP6_NF_MATCH_UNCLEAN) += ip6t_unclean.o
obj-$(CONFIG_IP6_NF_FILTER) += ip6table_filter.o
obj-$(CONFIG_IP6_NF_TARGET_REJECT) += ip6t_REJECT.o
obj-$(CONFIG_IP6_NF_TARGET_MIRROR) += ip6t_MIRROR.o
obj-$(CONFIG_IP6_NF_TARGET_TOS) += ip6t_TOS.o
obj-$(CONFIG_IP6_NF_TARGET_MARK) += ip6t_MARK.o
obj-$(CONFIG_IP6_NF_TARGET_REDIRECT) += ip6t_REDIRECT.o
obj-$(CONFIG_IP6_NF_TARGET_LOG) += ip6t_LOG.o
obj-$(CONFIG_IP6_NF_QUEUE) += ip6_queue.o


include $(TOPDIR)/Rules.make

ip6_conntrack.o: $(ip6_conntrack-obj)
	$(LD) -r -o $@ $(ip6_conntrack-obj)
