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


OBJS	:= utils.o route.o proc.o timer.o protocol.o packet.o \
		   arp.o ip.o raw.o icmp.o tcp.o udp.o devinet.o af_inet.o \
		   igmp.o ip_fw.o ipip.o

ifdef CONFIG_INET_RARP

OBJS	:= $(OBJS) rarp.o

endif

ifdef CONFIG_INET

ipv4.o:		$(OBJS)
		$(LD) -r -o ipv4.o $(OBJS)

else

ipv4.o:
		$(AR) rcs ipv4.o

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
