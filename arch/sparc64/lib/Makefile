# $Id: Makefile,v 1.1 1996/12/27 17:28:35 davem Exp $
# Makefile for Sparc library files..
#

CFLAGS := $(CFLAGS) -ansi

OBJS  = memset.o blockops.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

blockops.o: blockops.S
	$(CC) -ansi -c -o blockops.o blockops.S

memset.o: memset.S
	$(CC) -D__ASSEMBLY__ -ansi -c -o memset.o memset.S

dep:

include $(TOPDIR)/Rules.make
