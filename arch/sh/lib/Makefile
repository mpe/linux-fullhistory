#
# Makefile for SuperH-specific library files..
#

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = lib.a
L_OBJS  = delay.o memcpy.o memset.o memmove.o old-checksum.o \
	  checksum.o

include $(TOPDIR)/Rules.make
