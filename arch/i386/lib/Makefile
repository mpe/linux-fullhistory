#
# Makefile for i386-specific library files..
#

ifdef SMP
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o
else
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
endif

L_TARGET = lib.a
L_OBJS  = checksum.o semaphore.o locks.o delay.o usercopy.o getuser.o putuser.o

include $(TOPDIR)/Rules.make
