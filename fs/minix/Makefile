#
# Makefile for the Linux minix filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

O_TARGET := minix.o
O_OBJS   := bitmap.o truncate.o namei.o inode.o file.o dir.o symlink.o fsync.o
M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
