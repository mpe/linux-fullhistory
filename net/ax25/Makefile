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
.c.s:
	$(CC) $(CFLAGS) -S $<


OBJS	:= af_ax25.o

ifdef CONFIG_AX25

OBJS	:= $(OBJS) ax25_in.o ax25_out.o ax25_route.o ax25_subr.o ax25_timer.o

endif

ax25.o:		$(OBJS)
		$(LD) -r -o ax25.o $(OBJS)

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
