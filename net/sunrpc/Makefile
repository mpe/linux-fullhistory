#
# Makefile for Linux kernel SUN RPC
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := sunrpc.o
O_OBJS   := clnt.o xprt.o sched.o \
	    auth.o auth_null.o auth_unix.o \
	    svc.o svcsock.o svcauth.o \
	    pmap_clnt.o xdr.o sysctl.o
OX_OBJS  := sunrpc_syms.o

ifeq ($(CONFIG_PROC_FS),y)
  O_OBJS     += stats.o
endif

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
