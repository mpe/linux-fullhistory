#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ipv4.o
IPV4_OBJS := utils.o route.o proc.o timer.o protocol.o packet.o \
	     ip_input.o ip_fragment.o ip_forward.o ip_options.o \
	     ip_output.o ip_sockglue.o \
	     tcp.o tcp_input.o tcp_output.o tcp_timer.o \
	     raw.o udp.o arp.o icmp.o devinet.o af_inet.o igmp.o ip_fw.o \
	     sysctl_net_ipv4.o

MOD_LIST_NAME := IPV4_MODULES
M_OBJS :=

ifeq ($(CONFIG_IP_MROUTE),y)
IPV4_OBJS += ipmr.o
endif

ifeq ($(CONFIG_INET_RARP),y)
IPV4_OBJS += rarp.o
else
  ifeq ($(CONFIG_INET_RARP),m)
  M_OBJS += rarp.o
  endif
endif

ifeq ($(CONFIG_NET_IPIP),y)
IPV4_OBJS += ipip.o
else
  ifeq ($(CONFIG_NET_IPIP),m)
  M_OBJS += ipip.o
  endif
endif

ifeq ($(CONFIG_IP_MASQUERADE),y)
IPV4_OBJS += ip_masq.o ip_masq_app.o
M_OBJS += ip_masq_ftp.o ip_masq_irc.o ip_masq_raudio.o
endif

ifeq ($(CONFIG_IP_ALIAS),y)
IPV4_OBJS += ip_alias.o
else
  ifeq ($(CONFIG_IP_ALIAS),m)
  M_OBJS += ip_alias.o
  endif
endif

ifdef CONFIG_INET
O_OBJS := $(IPV4_OBJS)
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
