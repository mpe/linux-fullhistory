#
# Makefile for the Linux proc filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

O_TARGET := proc.o
O_OBJS   := inode.o root.o base.o generic.o mem.o link.o fd.o array.o \
		kmsg.o scsi.o proc_tty.o
ifdef CONFIG_OMIRR
O_OBJS   := $(O_OBJS) omirr.o
endif
OX_OBJS  := procfs_syms.o
M_OBJS   := 

ifeq ($(CONFIG_SUN_OPENPROMFS),y)
O_OBJS += openpromfs.o
else
  ifeq ($(CONFIG_SUN_OPENPROMFS),m)
  M_OBJS += openpromfs.o
  endif
endif

ifeq ($(CONFIG_PROC_DEVICETREE),y)
O_OBJS += proc_devtree.o
endif

include $(TOPDIR)/Rules.make
