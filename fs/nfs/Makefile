#
# Makefile for the Linux nfs filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

O_TARGET := nfs.o
O_OBJS   := inode.o file.o read.o write.o dir.o symlink.o proc.o \
	    nfs2xdr.o

ifdef CONFIG_ROOT_NFS
  O_OBJS += nfsroot.o mount_clnt.o
endif

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
