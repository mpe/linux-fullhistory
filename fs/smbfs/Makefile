#
# Makefile for the linux smb-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := smbfs.o
O_OBJS   := proc.o sock.o inode.o file.o dir.o ioctl.o mmap.o
M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
