#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := unix.o
O_OBJS	 := af_unix.o garbage.o sysctl_net_unix.o

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
