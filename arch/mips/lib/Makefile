# $Id: Makefile,v 1.8 1998/05/08 01:44:18 ralf Exp $
#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= csum_partial.o csum_partial_copy.o dump_tlb.o ide-std.o ide-no.o \
	  memset.o memcpy.o strlen_user.o strncpy_user.o tags.o watch.o

#
# Debug console, works without other support from the kernel
#
L_OBJS += tinycon.o

include $(TOPDIR)/Rules.make
