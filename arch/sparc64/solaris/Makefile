#
# Makefile for the Solaris binary emulation.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := solaris.o
O_OBJS   := entry64.o fs.o misc.o signal.o systbl.o socket.o ioctl.o ipc.o socksys.o timod.o
ifeq ($(CONFIG_SOLARIS_EMUL),m)
M_OBJS   := $(O_TARGET)
CPPFLAGS = $(MODFLAGS)
endif

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) $(CPPFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) $(CPPFLAGS) -ansi -c $< -o $*.o

ifneq ($(CONFIG_SOLARIS_EMUL),y)
do_it_all:
endif

include $(TOPDIR)/Rules.make
