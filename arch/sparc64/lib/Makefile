# $Id: Makefile,v 1.21 2000/03/27 10:38:41 davem Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS)

OBJS  = PeeCeeI.o blockops.o debuglocks.o strlen.o strncmp.o \
	memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	VIScopy.o VISbzero.o VISmemset.o VIScsum.o VIScsumcopy.o \
	VIScsumcopyusr.o VISsave.o atomic.o rwlock.o bitops.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

VIScopy.o: VIScopy.S VIS.h
VISbzero.o: VISbzero.S VIS.h

.S.s:
	$(CPP) $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) -ansi -c $< -o $*.o

dep:

include $(TOPDIR)/Rules.make
