# $Id: Makefile,v 1.13 1997/07/16 10:12:03 jj Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS) -ansi

OBJS  = blockops.o locks.o strlen.o strncmp.o \
	memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	VIScopy.o VISbzero.o VISmemset.o VIScsum.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

VIScopy.o: VIScopy.S VIS.h
VISbzero.o: VISbzero.S VIS.h

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

dep:

include $(TOPDIR)/Rules.make
