# $Id: Makefile,v 1.31 1999/12/28 11:50:39 jj Exp $
# Makefile for Sparc library files..
#

OBJS  = mul.o rem.o sdiv.o udiv.o umul.o urem.o ashrdi3.o memcpy.o memset.o \
        strlen.o checksum.o blockops.o memscan.o memcmp.o strncmp.o \
	strncpy_from_user.o divdi3.o udivdi3.o strlen_user.o \
	copy_user.o locks.o atomic.o bitops.o debuglocks.o lshrdi3.o \
	ashldi3.o rwsem.o

ifdef CONFIG_SMP
OBJS += irqlock.o
endif

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi -DST_DIV0=0x2 $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -DST_DIV0=0x2 -c $< -o $*.o

dep:

include $(TOPDIR)/Rules.make
