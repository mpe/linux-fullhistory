#
# Makefile for the linux fat-filesystem support.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := fat.o
O_OBJS   := buffer.o cache.o dir.o file.o inode.o misc.o mmap.o tables.o
OX_OBJS  := fatfs_syms.o
M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
