#
# Makefile for the Linux TCP/IP (INET) layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

.c.o:
	$(CC) $(CFLAGS) -c $<
.s.o:
	$(AS) -o $*.o $<


OBJS	:= af_netrom.o

ifdef CONFIG_AX25

OBJS	:= $(OBJS) nr_dev.o nr_in.o nr_out.o nr_route.o nr_subr.o nr_timer.o

endif

netrom.o:		$(OBJS)
		$(LD) -r -o netrom.o $(OBJS)

dep:
		$(CPP) -M *.c > .depend

tar:
		tar -cvf /dev/f1 .

modules:

include $(TOPDIR)/Rules.make
