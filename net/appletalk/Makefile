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


OBJS	:= aarp.o ddp.o


appletalk.o:	$(OBJS)
		$(LD) -r -o appletalk.o $(OBJS)

dep:
		$(CPP) -M *.c > .depend

tar:
		tar -cvf /dev/f1 .
modules:

include $(TOPDIR)/Rules.make
