#
# Makefile for m68k-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $@

L_TARGET = lib.a

obj-y		:= ashrdi3.o lshrdi3.o checksum.o memcpy.o memcmp.o memset.o \
		    semaphore.o muldi3.o

include $(TOPDIR)/Rules.make
