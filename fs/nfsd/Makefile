#
# Makefile for the linux nfs-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := nfsd.o
O_OBJS   := nfssvc.o nfsctl.o nfsproc.o nfsfh.o vfs.o \
	    export.o auth.o lockd.o nfscache.o nfsxdr.o

ifdef CONFIG_PROC_FS
  O_OBJS += stats.o
endif

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
