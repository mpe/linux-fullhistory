#
# Makefile for the Linux 802.x protocol layers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := 802.o
O_OBJS	  = p8023.o

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += sysctl_net_802.o
endif

ifeq ($(CONFIG_LLC),y)
SUB_DIRS += transit
O_OBJS += llc_sendpdu.o llc_utility.o cl2llc.o
OX_OBJS += llc_macinit.o p8022.o psnap.o p8022tr.o
endif

ifdef CONFIG_TR
O_OBJS += tr.o
endif

ifdef CONFIG_FDDI
O_OBJS += fddi.o
endif

ifdef CONFIG_IPX
OX_OBJS += p8022.o psnap.o p8022tr.o
endif

ifdef CONFIG_ATALK
ifndef CONFIG_IPX
OX_OBJS += p8022.o psnap.o p8022tr.o
endif
endif

include $(TOPDIR)/Rules.make

cl2llc.c: cl2llc.pre
	sed -f ./pseudo/opcd2num.sed cl2llc.pre >cl2llc.c

tar:
		tar -cvf /dev/f1 .
