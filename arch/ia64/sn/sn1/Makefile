#
# ia64/platform/sn/sn1/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

CFLAGS          :=     $(CFLAGS) -DCONFIG_SGI_SN1 -DSN1 -DSN -DSOFTSDV \
			-DLANGUAGE_C=1 -D_LANGUAGE_C=1
AFLAGS          :=      $(AFLAGS) -DCONFIG_SGI_SN1 -DSN1 -DSOFTSDV

.S.s:
	$(CPP) $(AFLAGS) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) -c -o $*.o $<

all: sn1.a

O_TARGET        = sn1.a
O_HEADERS       =
O_OBJS          = irq.o setup.o

ifeq ($(CONFIG_IA64_GENERIC),y)
O_OBJS		+= machvec.o
endif

clean::

include $(TOPDIR)/Rules.make
