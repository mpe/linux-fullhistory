# $Id: Makefile,v 1.21 1997/01/25 06:48:43 davem Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS) -ansi

OBJS  = mul.o rem.o sdiv.o udiv.o umul.o urem.o ashrdi3.o memcpy.o memset.o \
        strlen.o checksum.o blockops.o memscan.o memcmp.o strncmp.o \
	strncpy_from_user.o divdi3.o udivdi3.o strlen_user.o \
	copy_user.o clear_user.o locks.o atomic.o bitops.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

checksum.o: checksum.S
	$(CC) -ansi -c -o checksum.o checksum.S

memcpy.o: memcpy.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memcpy.o memcpy.S

memcmp.o: memcmp.S
	$(CC) -ansi -c -o memcmp.o memcmp.S

memscan.o: memscan.S
	$(CC) -ansi -c -o memscan.o memscan.S

strncmp.o: strncmp.S
	$(CC) -ansi -c -o strncmp.o strncmp.S

strncpy_from_user.o: strncpy_from_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o strncpy_from_user.o strncpy_from_user.S

strlen_user.o: strlen_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o strlen_user.o strlen_user.S

copy_user.o: copy_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o copy_user.o copy_user.S

clear_user.o: clear_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o clear_user.o clear_user.S

blockops.o: blockops.S
	$(CC) -ansi -c -o blockops.o blockops.S

memset.o: memset.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memset.o memset.S

ifdef SMP

locks.o: locks.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o locks.o locks.S

atomic.o: atomic.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o atomic.o atomic.S

bitops.o: bitops.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c -o bitops.o bitops.S

else

locks.o: locks.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o locks.o locks.S

atomic.o: atomic.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o atomic.o atomic.S

bitops.o: bitops.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o bitops.o bitops.S

endif

strlen.o: strlen.S
	$(CC) -ansi -c -o strlen.o strlen.S

divdi3.o: divdi3.S
	$(CC) -ansi -c -o divdi3.o divdi3.S

udivdi3.o: udivdi3.S
	$(CC) -ansi -c -o udivdi3.o udivdi3.S

mul.o: mul.S
	$(CC) -c -o mul.o mul.S

rem.o: rem.S
	$(CC) -DST_DIV0=0x2 -c -o rem.o rem.S

sdiv.o: sdiv.S
	$(CC) -DST_DIV0=0x2 -c -o sdiv.o sdiv.S

udiv.o: udiv.S
	$(CC) -DST_DIV0=0x2 -c -o udiv.o udiv.S

umul.o: umul.S
	$(CC) -c -o umul.o umul.S

urem.o: urem.S
	$(CC) -DST_DIV0=0x2 -c -o urem.o urem.S

ashrdi3.o: ashrdi3.S
	$(CC) -c -o ashrdi3.o ashrdi3.S

dep:

include $(TOPDIR)/Rules.make
