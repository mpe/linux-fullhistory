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
	  eth.o packet.o arp.o dev.o 8390.o wd.o ne.o el2.o hp.o plip.o \
	  slip.o slhc.o d_link.o auto_irq.o ip.o raw.o icmp.o tcp.o udp.o\
	  lance.o 3c509.o #ip-frag.o

ifdef CONFIG_INET

inet.o:		$(OBJS)
		$(LD) -r -o inet.o $(OBJS)

else

inet.o:
		echo | $(AS) -o inet.o

endif

CARDS :=
include	CONFIG


Space.o:	Space.c CONFIG /usr/include/linux/autoconf.h
		$(CC) $(CPPFLAGS) $(CFLAGS) $(OPTS) $(CARDS) $(DL_OPTS) \
							-c $< -o $@

8390.o:		8390.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(CARDS) -c $< -o $@

wd.o:		wd.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(WD_OPTS) -c $< -o $@

el2.o:		el2.c CONFIG el2reg.h
		$(CC) $(CPPFLAGS) $(CFLAGS) $(EL2_OPTS) -c $< -o $@

ne.o:		ne.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(NE_OPTS) -c $< -o $@

hp.o:		hp.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(HP_OPTS) -c $< -o $@

plip.o:		plip.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(PLIP_OPTS) -c $< -o $@

slip.o:		slip.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(SLIP_OPTS) -c $< -o $@

d_link.o:	d_link.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(DL_OPTS) -c $< -o $@

lance.o:	lance.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(AT_OPTS) -c $< -o $@

3c509.o:	3c509.c CONFIG
		$(CC) $(CPPFLAGS) $(CFLAGS) $(EL3_OPTS) -c $< -o $@

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
