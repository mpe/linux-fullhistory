# $Id: Makefile,v 1.33 2000/03/16 00:52:07 anton Exp $
# Makefile for Sparc library files..
#

OBJS  = mul.o rem.o sdiv.o udiv.o umul.o urem.o ashrdi3.o memcpy.o memset.o \
        strlen.o checksum.o blockops.o memscan.o memcmp.o strncmp.o \
	strncpy_from_user.o divdi3.o udivdi3.o strlen_user.o \
	copy_user.o locks.o atomic.o bitops.o debuglocks.o lshrdi3.o \
	ashldi3.o rwsem.o muldi3.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

.S.s:
	$(CPP) $(AFLAGS) -ansi -DST_DIV0=0x2 $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) -ansi -DST_DIV0=0x2 -c $< -o $*.o

dep:

include $(TOPDIR)/Rules.make
