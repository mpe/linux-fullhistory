#
# Makefile for MIPS-specific library files..
#
# $Id: Makefile,v 1.7 1998/05/04 09:18:03 ralf Exp $
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= csum_partial.o csum_partial_copy.o dump_tlb.o ide-std.c ide-no.o \
	  memset.o memcpy.o strlen_user.o strncpy_user.o tags.o watch.o

#
# Debug console, works without other support from the kernel
#
L_OBJS += tinycon.o

include $(TOPDIR)/Rules.make
