#
# ia64/platform/dig/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -c -o $*.o $<

all: dig.a

O_TARGET        = dig.a
O_OBJS          = iosapic.o setup.o

ifeq ($(CONFIG_IA64_GENERIC),y)
O_OBJS		+= machvec.o
endif

clean::

include $(TOPDIR)/Rules.make
