# $Id: Makefile,v 1.7 1997/04/07 18:57:05 jj Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS) -ansi

OBJS  = memset.o blockops.o locks.o memcpy.o strlen.o strncmp.o \
	memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	copy_to_user.o copy_from_user.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

blockops.o: blockops.S
	$(CC) -ansi -c -o blockops.o blockops.S

memset.o: memset.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memset.o memset.S

copy_to_user.o: copy_to_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o copy_to_user.o copy_to_user.S

copy_from_user.o: copy_from_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o copy_from_user.o copy_from_user.S

memcpy.o: memcpy.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memcpy.o memcpy.S

strlen.o: strlen.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o strlen.o strlen.S

strncmp.o: strncmp.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o strncmp.o strncmp.S

memcmp.o: memcmp.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memcmp.o memcmp.S

locks.o: locks.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o locks.o locks.S

checksum.o: checksum.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o checksum.o checksum.S

memscan.o: memscan.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memscan.o memscan.S

strncpy_from_user.o: strncpy_from_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o strncpy_from_user.o strncpy_from_user.S

strlen_user.o: strlen_user.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o strlen_user.o strlen_user.S

dep:

include $(TOPDIR)/Rules.make
