#
# Makefile for SuperH-specific library files..
#

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = lib.a
# L_OBJS  = checksum.o old-checksum.o semaphore.o delay.o \
# 	usercopy.o getuser.o putuser.o
L_OBJS  = delay.o memcpy.o memset.o memmove.o csum_partial_copy.o \
	  wordcopy.o checksum.o # usercopy.o getuser.o putuser.o

include $(TOPDIR)/Rules.make
