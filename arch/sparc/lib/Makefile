# $Id: Makefile,v 1.32 2000/02/28 04:00:48 anton Exp $
# Makefile for Sparc library files..
#

OBJS  = mul.o rem.o sdiv.o udiv.o umul.o urem.o ashrdi3.o memcpy.o memset.o \
        strlen.o checksum.o blockops.o memscan.o memcmp.o strncmp.o \
	strncpy_from_user.o divdi3.o udivdi3.o strlen_user.o \
	copy_user.o locks.o atomic.o bitops.o debuglocks.o lshrdi3.o \
	ashldi3.o rwsem.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi -DST_DIV0=0x2 $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -DST_DIV0=0x2 -c $< -o $*.o

dep:

include $(TOPDIR)/Rules.make
