#
# Makefile for the linux smb-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := smbfs.o
O_OBJS   := proc.o dir.o cache.o sock.o inode.o file.o ioctl.o 
M_OBJS   := $(O_TARGET)

# If you want debugging output, please uncomment the following line

# EXTRA_CFLAGS += -DDEBUG -DDEBUG_SMB_MALLOC=1

include $(TOPDIR)/Rules.make
