#
# Makefile for the linux networking.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

MOD_SUB_DIRS := ipv4
ALL_SUB_DIRS := 802 ax25 core ethernet ipv4 ipx unix appletalk netrom
SUB_DIRS     := 802 core ethernet unix

ifeq ($(CONFIG_INET),y)
SUB_DIRS += ipv4
endif

ifeq ($(CONFIG_IPX),y)
SUB_DIRS += ipx
endif

ifeq ($(CONFIG_ATALK),y)
SUB_DIRS += appletalk
endif

ifeq ($(CONFIG_NETROM),y)
SUB_DIRS += netrom
endif

ifeq ($(CONFIG_AX25),y)
SUB_DIRS += ax25
endif

L_TARGET     := network.a
L_OBJS	     := socket.o protocols.o netlink.o $(join $(SUB_DIRS),$(SUB_DIRS:%=/%.o))

include $(TOPDIR)/Rules.make
