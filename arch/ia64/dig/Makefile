#
# ia64/platform/dig/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

.S.s:
	$(CPP) $(AFLAGS) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) -c -o $*.o $<

all: dig.a

O_TARGET        = dig.a
O_OBJS          = iosapic.o setup.o

ifdef CONFIG_IA64_GENERIC
O_OBJS		+= machvec.o
endif

clean::

include $(TOPDIR)/Rules.make
