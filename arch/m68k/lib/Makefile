#
# Makefile for m68k-specific library files..
#

.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $@

L_TARGET = lib.a
L_OBJS  = ashrdi3.o checksum.o memcpy.o memcmp.o memset.o semaphore.o

include $(TOPDIR)/Rules.make
