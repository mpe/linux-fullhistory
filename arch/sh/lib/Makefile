#
# Makefile for SuperH-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = lib.a
L_OBJS  = delay.o memcpy.o memset.o memmove.o memchr.o old-checksum.o \
	  checksum.o strcasecmp.o

include $(TOPDIR)/Rules.make
