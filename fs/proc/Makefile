#
# Makefile for the linux proc-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := proc.o
O_OBJS   := inode.o root.o base.o mem.o link.o fd.o array.o kmsg.o net.o scsi.o
OX_OBJS  := procfs_syms.o
M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
