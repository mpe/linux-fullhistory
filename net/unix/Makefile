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


OBJS	:= af_unix.o


unix.o:	$(OBJS)
		$(LD) -r -o unix.o $(OBJS)

dep:
		$(CPP) -M *.c > .depend

modules:
tar:
		tar -cvf /dev/f1 .


include $(TOPDIR)/Rules.make
