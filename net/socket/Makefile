#
# Makefile for the Linux socket support layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

CFLAGS := $(CFLAGS) -I../inet -I..
CPP := $(CPP) -I../inet -I..

.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<
.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<


OBJS	= datagram.o dev.o skbuff.o sock.o

socket.o:	$(OBJS)
		$(LD) -r -o socket.o $(OBJS)

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
