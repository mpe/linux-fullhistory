#
# Makefile for the linux ncp-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := ncpfs.o
O_OBJS   := dir.o file.o inode.o ioctl.o mmap.o ncplib_kernel.o sock.o
M_OBJS   := $(O_TARGET)

# If you want debugging output, please uncomment the following line

# EXTRA_CFLAGS += -DDEBUG_NCP=1

include $(TOPDIR)/Rules.make

ncplib_kernel.o: ncplib_kernel.c ncplib_kernel.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -finline-functions -c -o $@ $<
