#
# Makefile for the linux nfs-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := nfs.o
O_OBJS   := proc.o sock.o rpcsock.o inode.o file.o bio.o \
	    nfsiod.o dir.o symlink.o

ifdef CONFIG_ROOT_NFS
O_OBJS += nfsroot.o
endif

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
