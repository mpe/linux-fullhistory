#
# Makefile for i386-specific library files..
#

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = lib.a
L_OBJS  = checksum.o semaphore.o delay.o usercopy.o getuser.o putuser.o lock.o lock_check.o

include $(TOPDIR)/Rules.make
