#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ipv4.o
IPV4_OBJS := utils.o route.o inetpeer.o proc.o protocol.o \
	     ip_input.o ip_fragment.o ip_forward.o ip_options.o \
	     ip_output.o ip_sockglue.o \
	     tcp.o tcp_input.o tcp_output.o tcp_timer.o tcp_ipv4.o tcp_minisocks.o \
	     raw.o udp.o arp.o icmp.o devinet.o af_inet.o igmp.o \
	     sysctl_net_ipv4.o fib_frontend.o fib_semantics.o fib_hash.o
IPV4X_OBJS :=

M_OBJS :=

ifeq ($(CONFIG_IP_MULTIPLE_TABLES),y)
IPV4_OBJS += fib_rules.o
endif

ifeq ($(CONFIG_IP_ROUTE_NAT),y)
IPV4_OBJS += ip_nat_dumb.o
endif

ifeq ($(CONFIG_IP_MROUTE),y)
IPV4_OBJS += ipmr.o
endif

ifeq ($(CONFIG_NET_IPIP),y)
IPV4X_OBJS += ipip.o
else
  ifeq ($(CONFIG_NET_IPIP),m)
  MX_OBJS += ipip.o
  endif
endif

ifeq ($(CONFIG_NET_IPGRE),y)
IPV4X_OBJS += ip_gre.o
else
  ifeq ($(CONFIG_NET_IPGRE),m)
  MX_OBJS += ip_gre.o
  endif
endif

ifeq ($(CONFIG_SYN_COOKIES),y)
IPV4_OBJS += syncookies.o
# module not supported, because it would be too messy.
endif

ifeq ($(CONFIG_IP_PNP),y)
IPV4_OBJS += ipconfig.o
endif

ifdef CONFIG_INET
O_OBJS := $(IPV4_OBJS)
OX_OBJS := $(IPV4X_OBJS)
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
