#
# Makefile for the linux lock manager stuff
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := lockd.o
O_OBJS   := clntlock.o clntproc.o host.o svc.o svclock.o svcshare.o \
	    svcproc.o svcsubs.o mon.o xdr.o
OX_OBJS  := lockd_syms.o
M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
