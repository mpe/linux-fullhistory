# $Id: Makefile,v 1.18 1998/10/13 09:07:24 davem Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS)

OBJS  = PeeCeeI.o blockops.o debuglocks.o strlen.o strncmp.o \
	memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	VIScopy.o VISbzero.o VISmemset.o VIScsum.o VIScsumcopy.o VISsave.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

VIScopy.o: VIScopy.S VIS.h
VISbzero.o: VISbzero.S VIS.h

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $< -o $*.o

dep:

include $(TOPDIR)/Rules.make
