#
# Makefile for the Linux TCP/IP (INET6) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#


O_TARGET := ipv6.o
O_OBJS   := af_inet6.o ipv6_output.o ipv6_input.o addrconf.o sit.o \
	    ipv6_route.o ipv6_sockglue.o ndisc.o udp.o raw.o \
	    protocol.o icmp.o mcast.o reassembly.o tcp_ipv6.o \
	    exthdrs.o sysctl_net_ipv6.o datagram.o

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
