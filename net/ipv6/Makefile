#
# Makefile for the Linux TCP/IP (INET6) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#


O_TARGET := ipv6.o
IPV6_OBJS :=	af_inet6.o ip6_output.o ip6_input.o addrconf.o sit.o \
		route.o ip6_fib.o ipv6_sockglue.o ndisc.o udp.o raw.o \
		protocol.o icmp.o mcast.o reassembly.o tcp_ipv6.o \
		exthdrs.o sysctl_net_ipv6.o datagram.o proc.o

MOD_LIST_NAME := IPV6_MODULES
M_OBJS   := $(O_TARGET)

#ifeq ($(CONFIG_IPV6_FIREWALL),y)
#  IPV6_OBJS += ip6_fw.o
#endif

O_OBJS := $(IPV6_OBJS)

include $(TOPDIR)/Rules.make
