#
# Makefile for the Linux networking core.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := core.o

O_OBJS	:= sock.o skbuff.o iovec.o datagram.o dst.o scm.o \
	neighbour.o rtnetlink.o

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_core.o
endif

ifdef CONFIG_NET

O_OBJS	+= dev.o dev_mcast.o

ifdef CONFIG_FIREWALL
OX_OBJS += firewall.o
endif

endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
