#
# Makefile for the linux networking.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

# only these two lines should need to be changed to remove inet sockets.
# (and the inet/tcpip.o in net.o)

DRIVERS     = drv
SUBDIRS     := unix inet

.c.o:
	$(CC) $(CFLAGS) -c $<
.s.o:
	$(AS) -o $*.o $<
.c.s:
	$(CC) $(CFLAGS) -S $<

OBJS	=  Space.o ddi.o socket.o

all:		net.o

net.o:		$(OBJS) subdirs
		$(LD) -r -o net.o $(OBJS) $(DRIVERS)/$(DRIVERS).a network.a

subdirs:	dummy
		@rm -f network.a
		@for i in $(DRIVERS); do (cd $$i && echo $$i && $(MAKE) $$i.a) || exit; done
		@for i in $(SUBDIRS); do (cd $$i && echo $$i && $(MAKE) && ar rcs ../network.a $$i.o) || exit; done
		@ranlib network.a

clean:
		rm -f core *.o *.a *.s .depend
		@for i in $(DRIVERS) $(SUBDIRS); do (cd $$i && echo $$i && $(MAKE) clean) || exit; done

dep:
		$(CPP) -M *.c > .depend
		@for i in $(DRIVERS) $(SUBDIRS); do (cd $$i && echo $$i && $(MAKE) dep) || exit; done

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

