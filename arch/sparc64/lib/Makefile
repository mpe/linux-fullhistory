# $Id: Makefile,v 1.15 1997/08/19 03:11:50 davem Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS)

OBJS  = PeeCeeI.o blockops.o locks.o strlen.o strncmp.o \
	memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	VIScopy.o VISbzero.o VISmemset.o VIScsum.o VIScsumcopy.o

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
