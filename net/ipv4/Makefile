#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ipv4.o
IPV4_OBJS	:= utils.o route.o proc.o timer.o protocol.o packet.o \
		   arp.o ip.o raw.o icmp.o tcp.o udp.o devinet.o af_inet.o \
		   igmp.o ip_fw.o ipip.o

ifdef CONFIG_INET_RARP
IPV4_OBJS := $(IPV4_OBJS) rarp.o
else
M_OBJS := rarp.o
MOD_LIST_NAME := IPV4_MODULES
endif

ifdef CONFIG_INET
O_OBJS := $(IPV4_OBJS)
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
