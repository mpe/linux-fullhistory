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


OBJS	:= sock.o eth.o dev.o dev_mcast.o skbuff.o datagram.o

ifdef CONFIG_INET

OBJS	:= $(OBJS) utils.o route.o proc.o timer.o protocol.o packet.o \
		   arp.o ip.o raw.o icmp.o tcp.o udp.o devinet.o af_inet.o \
		   igmp.o ip_fw.o 

ifdef CONFIG_INET_RARP

OBJS	:= $(OBJS) rarp.o

endif
endif

ifdef CONFIG_AX25

OBJS	:= $(OBJS) ax25.o ax25_in.o ax25_out.o ax25_route.o ax25_subr.o ax25_timer.o

endif

ifdef CONFIG_IPX

OBJS	:= $(OBJS) ipx.o pe2.o p8022.o p8023.o
SNAPO	:= psnap.o

endif

ifdef CONFIG_ATALK

OBJS	:= $(OBJS) aarp.o ddp.o
SNAPO	:= psnap.o
endif

OBJS	:= $(OBJS) $(SNAPO)

ifdef CONFIG_NET

inet.o:		$(OBJS)
		$(LD) -r -o inet.o $(OBJS)

else

inet.o:
		$(AR) rcs inet.o

endif

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
