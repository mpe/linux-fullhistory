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


OBJS	:= eth.o 

ifdef CONFIG_IPX

OBJ2	:= pe2.o

endif

ifdef CONFIG_ATALK

OBJ2	:= pe2.o

endif

OBJS	:= $(OBJS) $(OBJ2)

ifdef CONFIG_NET

ethernet.o:	$(OBJS)
		$(LD) -r -o ethernet.o $(OBJS)

else

ethernet.o:
		$(AR) rcs ethernet.o

endif

dep:
		$(CPP) -M *.c > .depend

tar:
		tar -cvf /dev/f1 .


include $(TOPDIR)/Rules.make
