#
# Makefile for the Linix TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<
.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<


OBJS	= Space.o sock.o utils.o route.o proc.o timer.o protocol.o loopback.o \
	  eth.o packet.o arp.o dev.o 8390.o wd.o ne.o el.o hp.o plip.o \
	  slip.o d_link.o auto_irq.o ip.o raw.o icmp.o tcp.o udp.o

ifdef CONFIG_INET

inet.o:		$(OBJS)
		$(LD) -r -o inet.o $(OBJS)

else

inet.o:
		echo | $(AS) -o inet.o

endif

include	CONFIG


Space.o:	CONFIG Space.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(OPTS) $(CARDS) $(DL_OPTS) \
							-c Space.c -o $@

8390.o:		CONFIG 8390.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(CARDS) -c 8390.c -o $@

wd.o:		CONFIG wd.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(WD_OPTS) -c wd.c -o $@

el.o:		CONFIG el.c elreg.h Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) -UEL2_AUI -c el.c -o $@

ne.o:		CONFIG ne.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(NE_OPTS) -c ne.c -o $@

hp.o:		CONFIG hp.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(HP_OPTS) -c hp.c -o $@

plip.o:		CONFIG plip.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(PLIP_OPTS) -c plip.c -o $@

slip.o:		CONFIG slip.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(SLIP_OPTS) -c slip.c -o $@

d_link.o:	CONFIG d_link.c Makefile
		$(CC) $(CPPFLAGS) $(CFLAGS) $(DL_OPTS) -c d_link.c -o $@

subdirs:	dummy
		for i in $(SUBDIRS); do (cd $$i; $(MAKE)); done


clean:
		rm -f core *.o *.a *.s

dep:
		$(CPP) -M *.c > .depend

tar:
		tar -cvf /dev/f1 .

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
