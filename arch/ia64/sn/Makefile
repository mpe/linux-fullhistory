#
# ia64/sn/Makefile
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

all: sn.a

O_TARGET        = sn.a
O_HEADERS       =
O_OBJS          = sn1/sn1.a

clean::

include $(TOPDIR)/Rules.make
