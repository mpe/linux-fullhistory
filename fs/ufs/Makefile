#
# Makefile for the Linux ufs filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

O_TARGET := ufs.o
O_OBJS   := ufs_dir.o ufs_file.o ufs_inode.o ufs_namei.o \
		ufs_super.o ufs_symlink.o ufs_swab.o
M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
