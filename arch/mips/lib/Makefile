# $Id: Makefile,v 1.11 1999/10/17 19:55:22 harald Exp $
#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= csum_partial.o csum_partial_copy.o floppy-std.o \
	  floppy-no.o ide-std.o ide-no.o kbd-std.o kbd-no.o rtc-std.o \
	  rtc-no.o memcpy.o memset.o strlen_user.o strncpy_user.o \
	  strnlen_user.o watch.o

ifdef CONFIG_CPU_R3000
  L_OBJS += r3k_dump_tlb.o 
else
  L_OBJS += dump_tlb.o 
endif

include $(TOPDIR)/Rules.make
